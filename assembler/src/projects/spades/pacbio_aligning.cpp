//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "assembly_graph/graph_alignment/pacbio/pac_index.hpp"
#include "assembly_graph/graph_alignment/pacbio/pacbio_gap_closer.hpp"
#include "assembly_graph/graph_alignment/long_read_storage.hpp"
#include "io/reads_io/wrapper_collection.hpp"
#include "assembly_graph/stats/picture_dump.hpp"
#include "pacbio_aligning.hpp"
#include "pair_info_count.hpp"
#include "assembly_graph/graph_alignment/long_read_mapper.hpp"
#include "io/reads_io/multifile_reader.hpp"

namespace debruijn_graph {

class GapTrackingLongReadMapper : public LongReadMapper {
    typedef LongReadMapper base;
    pacbio::GapStorage<Graph>& gap_storage_;
    const pacbio::GapStorage<Graph> empty_storage_;
    vector<pacbio::GapStorage<Graph>> buffer_storages_;

    bool IsTip(VertexId v) const {
        return g().IncomingEdgeCount(v) + g().OutgoingEdgeCount(v) == 1;
    }

    Sequence ExtractSubseq(const io::SingleRead& read, size_t start, size_t end) const {
        auto subread = read.Substr(start, end);
        if (subread.IsValid()) {
            return subread.sequence();
        } else {
            return Sequence();
        }
    }

    Sequence ExtractSubseq(const io::SingleReadSeq& read, size_t start, size_t end) const {
        return read.sequence().Subseq(start, end);
    }

    //FIXME should additional thresholds on gaps be introduced here?
    template<class ReadT>
    vector<GapDescription<Graph>> InferGaps(const ReadT& read, const MappingPath<EdgeId>& mapping) const {
        vector<GapDescription<Graph>> answer;
        for (size_t i = 0; i < mapping.size() - 1; ++i) {
            EdgeId e1 = mapping.edge_at(i);
            EdgeId e2 = mapping.edge_at(i + 1);

            //sorry, loops!
            if (e1 != e2 && IsTip(g().EdgeEnd(e1)) && IsTip(g().EdgeStart(e2))) {
                MappingRange mr1 = mapping.mapping_at(i);
                MappingRange mr2 = mapping.mapping_at(i + 1);
                auto gap_seq = ExtractSubseq(read, mr1.initial_range.end_pos, mr2.initial_range.start_pos);
                if (gap_seq.size() > 0) {
                    //FIXME are the positions correct?!!!
                    answer.push_back(GapDescription<Graph>(e1, e2,
                                                           gap_seq,
                                                           mr1.mapped_range.end_pos,
                                                           mr2.mapped_range.start_pos));
                }
            }
        }
        return answer;
    }

    template<class ReadT>
    void InnerProcessRead(size_t thread_index, const ReadT& read, const MappingPath<EdgeId>& mapping) {
        base::ProcessSingleRead(thread_index, read, mapping);
        for (const auto& gap: InferGaps(read, mapping)) {
            //FIXME should it really be "true" here?
            buffer_storages_[thread_index].AddGap(gap, true);
        }
    }

public:

    //ALERT passed path_storage should be empty!
    GapTrackingLongReadMapper(const Graph& g,
                              PathStorage<Graph>& path_storage,
                              pacbio::GapStorage<Graph>& gap_storage,
                              PathExtractionF path_extractor = nullptr) :
            base(g, path_storage, path_extractor),
            gap_storage_(gap_storage), empty_storage_(gap_storage)
    {
        VERIFY(empty_storage_.size() == 0);
    }


public:

    void StartProcessLibrary(size_t threads_count) override {
        base::StartProcessLibrary(threads_count);
        for (size_t i = 0; i < threads_count; ++i)
            buffer_storages_.push_back(empty_storage_);
    }

    void StopProcessLibrary() override {
        base::StopProcessLibrary();
        buffer_storages_.clear();
    }

    void MergeBuffer(size_t thread_index) override {
        DEBUG("Merge buffer " << thread_index << " with size " << buffer_storages_[thread_index].size());
        gap_storage_.AddStorage(buffer_storages_[thread_index]);
        buffer_storages_[thread_index].clear();
        DEBUG("Now size " << gap_storage_.size());
    }

    void ProcessSingleRead(size_t thread_index,
                           const io::SingleRead& read,
                           const MappingPath<EdgeId>& mapping) override {
        InnerProcessRead(thread_index, read, mapping);
    }

    void ProcessSingleRead(size_t thread_index,
                           const io::SingleReadSeq& read,
                           const MappingPath<EdgeId>& mapping) override {
        InnerProcessRead(thread_index, read, mapping);
    }

};

bool IsNontrivialAlignment(const vector<vector<EdgeId>>& aligned_edges) {
    for (size_t j = 0; j < aligned_edges.size(); j++)
        if (aligned_edges[j].size() > 1)
            return true;
    return false;
}

io::SingleStreamPtr GetReadsStream(const io::SequencingLibrary<config::DataSetData> &lib) {
    io::ReadStreamList<io::SingleRead> streams;
    for (const auto& reads : lib.single_reads())
        //do we need input_file function here?
        //FIXME should FixingWrapper be here?
        streams.push_back(make_shared<io::FixingWrapper>(make_shared<io::FileReadStream>(reads)));
    return io::MultifileWrap(streams);
}

class PacbioAligner {
    const conj_graph_pack& gp_;
    const pacbio::PacBioMappingIndex<ConjugateDeBruijnGraph>& pac_index_;
    PathStorage<Graph>& path_storage_;
    pacbio::GapStorage<Graph>& gap_storage_;
    pacbio::StatsCounter stats_;
    const PathStorage<Graph> empty_path_storage_;
    const pacbio::GapStorage<Graph> empty_gap_storage_;
    const size_t read_buffer_size_;

    void ProcessReadsBatch(const std::vector<io::SingleRead>& reads, size_t thread_cnt) {
        vector<PathStorage<Graph>> long_reads_by_thread(thread_cnt,
                                                        empty_path_storage_);
        vector<pacbio::GapStorage<Graph>> gaps_by_thread(thread_cnt,
                                                         empty_gap_storage_);
        vector<pacbio::StatsCounter> stats_by_thread(thread_cnt);

        size_t longer_500 = 0;
        size_t aligned = 0;
        size_t nontrivial_aligned = 0;

    #   pragma omp parallel for reduction(+: longer_500, aligned, nontrivial_aligned)
        for (size_t i = 0; i < reads.size(); ++i) {
            size_t thread_num = omp_get_thread_num();
            Sequence seq(reads[i].sequence());
            auto current_read_mapping = pac_index_.GetReadAlignment(seq);
            for (const auto& gap : current_read_mapping.gaps)
                gaps_by_thread[thread_num].AddGap(gap, true);

            const auto& aligned_edges = current_read_mapping.main_storage;
            for (const auto& path : aligned_edges)
                long_reads_by_thread[thread_num].AddPath(path, 1, true);

            //counting stats:
            for (const auto& path : aligned_edges)
                stats_by_thread[thread_num].path_len_in_edges[path.size()]++;

            if (seq.size() > 500) {
                longer_500++;
                if (aligned_edges.size() > 0) {
                    aligned++;
                    stats_by_thread[thread_num].seeds_percentage[
                            size_t(floor(double(current_read_mapping.seed_num) * 1000.0
                                         / (double) seq.size()))]++;

                    if (IsNontrivialAlignment(aligned_edges)) {
                        nontrivial_aligned++;
                    }
                }
            }
        }

        INFO("Read batch of size: " << reads.size() << " processed; "
                                    << longer_500 << " of them longer than 500; among long reads aligned: "
                                    << aligned << "; paths of more than one edge received: "
                                    << nontrivial_aligned );

        for (size_t i = 0; i < thread_cnt; i++) {
            path_storage_.AddStorage(long_reads_by_thread[i]);
            gap_storage_.AddStorage(gaps_by_thread[i]);
            stats_.AddStorage(stats_by_thread[i]);
        }
    }

public:
    PacbioAligner(const conj_graph_pack &gp,
                  const pacbio::PacBioMappingIndex<ConjugateDeBruijnGraph>& pac_index,
                  PathStorage<Graph>& path_storage,
                  pacbio::GapStorage<Graph>& gap_storage,
                  size_t read_buffer_size = 50000) :
            gp_(gp),
            pac_index_(pac_index),
            path_storage_(path_storage),
            gap_storage_(gap_storage),
            empty_path_storage_(path_storage),
            empty_gap_storage_(gap_storage),
            read_buffer_size_(read_buffer_size) {
        VERIFY(empty_path_storage_.size() == 0);
        VERIFY(empty_gap_storage_.size() == 0);
    }

    void operator()(io::SingleStream& read_stream, size_t thread_cnt) {
        size_t n = 0;
        size_t buffer_no = 0;
        while (!read_stream.eof()) {
            std::vector<io::SingleRead> read_buffer;
            read_buffer.reserve(read_buffer_size_);
            io::SingleRead read;
            for (size_t buf_size = 0; buf_size < read_buffer_size_ && !read_stream.eof(); ++buf_size) {
                read_stream >> read;
                read_buffer.push_back(std::move(read));
            }
            INFO("Prepared batch " << buffer_no << " of " << read_buffer.size() << " reads.");
            DEBUG("master thread number " << omp_get_thread_num());
            ProcessReadsBatch(read_buffer, thread_cnt);
            ++buffer_no;
            n += read_buffer.size();
            INFO("Processed " << n << " reads");
        }
    }

    const pacbio::StatsCounter& stats() const {
        return stats_;
    }
};

void PacbioAlignLibrary(const conj_graph_pack &gp,
                  const io::SequencingLibrary<config::DataSetData>& lib,
                  PathStorage<Graph>& path_storage,
                  pacbio::GapStorage<ConjugateDeBruijnGraph>& gap_storage,
                  size_t thread_cnt) {
    INFO("Aligning library with Pacbio aligner");

    INFO("Using seed size: " << cfg::get().pb.pacbio_k);

    //initializing index
    pacbio::PacBioMappingIndex<ConjugateDeBruijnGraph> pac_index(gp.g,
                                                                 cfg::get().pb.pacbio_k,
                                                                 cfg::get().K,
                                                                 cfg::get().pb.ignore_middle_alignment,
                                                                 cfg::get().output_dir,
                                                                 cfg::get().pb);

    PacbioAligner aligner(gp, pac_index, path_storage, gap_storage);

    auto stream = GetReadsStream(lib);
    aligner(*stream, thread_cnt);

    INFO("For library of " << (lib.is_long_read_lib() ? "long reads" : "contigs") << " :");
    aligner.stats().report();
    INFO("PacBio aligning finished");
}

void CloseGaps(conj_graph_pack &gp, bool rtype,
               const pacbio::GapStorage<Graph>& gap_storage) {
    INFO("Closing gaps with long reads");
    pacbio::PacbioGapCloser<Graph> gap_closer(gp.g, rtype, cfg::get().pb.max_contigs_gap_length);
    auto replacement = gap_closer.CloseGapsInGraph(gap_storage, cfg::get().max_threads);
    for (size_t j = 0; j < cfg::get().ds.reads.lib_count(); j++) {
        gp.single_long_reads[j].ReplaceEdges(replacement);
    }

    gap_closer.DumpToFile(cfg::get().output_saves + "gaps_pb_closed.fasta");
    INFO("Closing gaps with long reads finished");
}

bool ShouldAlignWithPacbioAligner(io::LibraryType lib_type) {
    return lib_type == io::LibraryType::PacBioReads ||
           lib_type == io::LibraryType::SangerReads ||
           lib_type == io::LibraryType::NanoporeReads;
}

void HybridLibrariesAligning::run(conj_graph_pack &gp, const char*) {
    using namespace omnigraph;

    bool launch_flag = false;
    bool make_additional_saves = parent_->saves_policy().make_saves_;
    for (size_t lib_id = 0; lib_id < cfg::get().ds.reads.lib_count(); ++lib_id) {
        if (cfg::get().ds.reads[lib_id].is_hybrid_lib()) {
            launch_flag = true;
            INFO("Pacbio-alignable library detected: #" << lib_id);

            const auto& lib = cfg::get().ds.reads[lib_id];
            bool rtype = lib.is_long_read_lib();

            size_t min_gap_quantity = rtype ? cfg::get().pb.pacbio_min_gap_quantity
                                            : cfg::get().pb.contigs_min_gap_quantity;

            PathStorage<Graph>& path_storage = gp.single_long_reads[lib_id];
            pacbio::GapStorage<ConjugateDeBruijnGraph> gap_storage(gp.g,
                                                                   min_gap_quantity,
                                                                   cfg::get().pb.long_seq_limit);

            if (ShouldAlignWithPacbioAligner(lib.type())) {
                //TODO put alternative alignment right here
                PacbioAlignLibrary(gp, lib,
                                   path_storage, gap_storage,
                                   cfg::get().max_threads);
            } else {
                GapTrackingLongReadMapper mapping_listener(gp.g, path_storage,
                                                           gap_storage,
                                                           ChooseProperReadPathExtractor(gp.g, lib.type()));
                ProcessSingleReads(gp, lib_id,
                                   /*use binary*/false, /*map_paired*/false,
                                   &mapping_listener);
            }

            if (make_additional_saves) {
                path_storage.DumpToFile(cfg::get().output_saves + "long_reads_before_rep.mpr",
                                        map<EdgeId, EdgeId>(), /*min_stats_cutoff*/rtype ? 1 : 0, true);
                gap_storage.DumpToFile(cfg::get().output_saves + "gaps.mpr");
            }

            //FIXME move inside AlignLibrary
            gap_storage.PadGapStrings();

            if (make_additional_saves)
                gap_storage.DumpToFile(cfg::get().output_saves +  "gaps_padded.mpr");

            CloseGaps(gp, rtype, gap_storage);
        }
    }

    if (launch_flag) {
        omnigraph::DefaultLabeler<Graph> labeler(gp.g, gp.edge_pos);
        stats::detail_info_printer printer(gp, labeler, cfg::get().output_dir);
        printer(config::info_printer_pos::final_gap_closed);
    }
}

}
