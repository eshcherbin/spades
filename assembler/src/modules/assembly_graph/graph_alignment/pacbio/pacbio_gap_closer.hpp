//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "pacbio_read_structures.hpp"

#include "ConsensusCore/Poa/PoaConfig.hpp"
#include "ConsensusCore/Poa/PoaConsensus.hpp"

#include <algorithm>
#include <fstream>

namespace pacbio {

template<class Graph>
class GapStorage {
    typedef typename Graph::EdgeId EdgeId;
    DECL_LOGGER("PacbioGaps");

    const Graph& g_;
    map<EdgeId, vector<GapDescription<Graph>>> inner_index_;
    vector<EdgeId> index_;
    set<pair<EdgeId, EdgeId>> nonempty_pairs_;
    set<pair<EdgeId, EdgeId>> transitively_ignored_pairs_;
    set<pair<EdgeId, EdgeId>> symmetrically_ignored_pairs_;

    void HiddenAddGap(const GapDescription<Graph> &p) {
        inner_index_[p.start].push_back(p);
    }

    void PadGapStrings(EdgeId e) {
        sort(inner_index_[e].begin(), inner_index_[e].end());
        auto cl_start = inner_index_[e].begin();
        auto iter = inner_index_[e].begin();
        vector<GapDescription<Graph> > padded_gaps;
        while (iter != inner_index_[e].end()) {
            auto next_iter = ++iter;
            if (next_iter == inner_index_[e].end() || next_iter->end != cl_start->end) {
                int start_min = std::numeric_limits<int>::max();
                int end_max = 0;
                size_t long_seqs = 0;
                size_t short_seqs = 0;
                bool exclude_long_seqs = false;
                for (auto j_iter = cl_start; j_iter != next_iter; j_iter++) {
                    //FIXME magic constants
                    if (g_.length(j_iter->start) - j_iter->edge_gap_start_position > 500 || j_iter->edge_gap_end_position > 500) {
                        DEBUG("ignoring alingment to the middle of edge");
                        continue;
                    }
                    if (j_iter->gap_seq.size() > long_seq_limit)
                        long_seqs++;
                    else
                        short_seqs++;

                    if (j_iter->edge_gap_start_position < start_min)
                        start_min = j_iter->edge_gap_start_position;
                    if (j_iter->edge_gap_end_position > end_max)
                        end_max = j_iter->edge_gap_end_position;
                }

                if (short_seqs >= min_gap_quantity && short_seqs > long_seqs)
                    exclude_long_seqs = true;

                for (auto j_iter = cl_start; j_iter != next_iter; j_iter++) {
                    if (g_.length(j_iter->start) - j_iter->edge_gap_start_position > 500 || j_iter->edge_gap_end_position > 500)
                        continue;

                    if (exclude_long_seqs && j_iter->gap_seq.size() > long_seq_limit)
                        continue;

                    string s = g_.EdgeNucls(j_iter->start).Subseq(start_min, j_iter->edge_gap_start_position).str();
                    s += j_iter->gap_seq.str();
                    s += g_.EdgeNucls(j_iter->end).Subseq(j_iter->edge_gap_end_position, end_max).str();
                    padded_gaps.push_back(GapDescription<Graph>(j_iter->start, j_iter->end, Sequence(s), start_min, end_max));
                }
                cl_start = next_iter;
            }
        }
        inner_index_[e] = padded_gaps;
    }

    void PostProcess() {
        FillIndex();

        for (auto j_iter = index_.begin(); j_iter != index_.end(); j_iter++) {
            EdgeId e = *j_iter;
            auto cl_start = inner_index_[e].begin();
            auto iter = inner_index_[e].begin();
            vector<GapDescription<Graph>> padded_gaps;
            while (iter != inner_index_[e].end()) {
                auto next_iter = ++iter;
                if (next_iter == inner_index_[e].end() || next_iter->end != cl_start->end) {
                    size_t len = next_iter - cl_start;
                    if (len >= min_gap_quantity) {
                        nonempty_pairs_.insert(make_pair(cl_start->start, cl_start->end));
                    }
                    cl_start = next_iter;
                }
            }
        }

        set<pair<EdgeId, EdgeId> > used_rc_pairs;
        for (auto iter = nonempty_pairs_.begin(); iter != nonempty_pairs_.end(); ++iter) {
            if (used_rc_pairs.find(*iter) != used_rc_pairs.end()) {
                DEBUG("skipping pair " << g_.int_id(iter->first) << "," << g_.int_id(iter->second));
                symmetrically_ignored_pairs_.insert(make_pair(iter->first, iter->second));
            } else {
                DEBUG("Using pair" << g_.int_id(iter->first) << "," << g_.int_id(iter->second));
            }

            for (size_t i = 0; i < index_.size(); i++) {
                if (nonempty_pairs_.find(make_pair(iter->first, index_[i])) != nonempty_pairs_.end()
                    && nonempty_pairs_.find(make_pair(index_[i], iter->second)) != nonempty_pairs_.end()) {
                    DEBUG("pair " << g_.int_id(iter->first) << "," << g_.int_id(iter->second) << " is ignored because of edge between " << g_.int_id(index_[i]));
                    transitively_ignored_pairs_.insert(make_pair(iter->first, iter->second));
                }
            }
            used_rc_pairs.insert(make_pair(g_.conjugate(iter->second), g_.conjugate(iter->first)));
        }
    }

public:
    const size_t min_gap_quantity;
    const size_t long_seq_limit;

    GapStorage(const Graph &g, size_t min_gap_quantity, size_t long_seq_limit)
            : g_(g),
              min_gap_quantity(min_gap_quantity), 
              long_seq_limit(long_seq_limit){
    }

    const map<EdgeId, vector<GapDescription<Graph>>>& inner_index() const {
        return inner_index_;
    };

    size_t FillIndex() {
        index_.resize(0);
        set<EdgeId> tmp;
        for (auto iter = inner_index_.begin(); iter != inner_index_.end(); iter++) {
            index_.push_back(iter->first);
        }
        return index_.size();
    }

    EdgeId operator[](size_t i) const {
        return index_.at(i);
    }

    size_t size() const {
        return index_.size();
    }

    bool IsTransitivelyIgnored(pair<EdgeId, EdgeId> p) const {
        return (transitively_ignored_pairs_.find(p) != transitively_ignored_pairs_.end());
    }

    bool IsSymmetricallyIgnored(pair<EdgeId, EdgeId> p) const {
        return (symmetrically_ignored_pairs_.find(p) != symmetrically_ignored_pairs_.end());
    }

    bool IsIgnored(pair<EdgeId, EdgeId> p) const {
        return (IsTransitivelyIgnored(p) || IsSymmetricallyIgnored(p));
    }

    void AddGap(const GapDescription<Graph> &p, bool add_rc = false) {
        HiddenAddGap(p);
        if (add_rc) {
            TRACE("Adding conjugate");
            HiddenAddGap(p.conjugate(g_, (int) g_.k()));
        }
    }

    void AddStorage(const GapStorage<Graph>& to_add) {
        const auto& idx = to_add.inner_index_;
        for (auto iter = idx.begin(); iter != idx.end(); ++iter)
            inner_index_[iter->first].insert(inner_index_[iter->first].end(), iter->second.begin(), iter->second.end());
    }

    void clear() {
        GapStorage empty(g_, min_gap_quantity, long_seq_limit);
        std::swap(inner_index_, empty.inner_index_);
        std::swap(index_, empty.index_);
        std::swap(nonempty_pairs_, empty.nonempty_pairs_);
        std::swap(transitively_ignored_pairs_, empty.transitively_ignored_pairs_);
        std::swap(symmetrically_ignored_pairs_, empty.symmetrically_ignored_pairs_);
    }

    void DumpToFile(const string filename) const {
        ofstream filestr(filename);
        for (const auto& e_gaps : inner_index_) {
            EdgeId e = e_gaps.first;
            auto gaps = e_gaps.second;
            DEBUG(g_.int_id(e)<< " " << gaps.size());
            filestr << g_.int_id(e) << " " << gaps.size() << endl;
            sort(gaps.begin(), gaps.end());
            for (const auto& gap : gaps) {
                filestr << gap.str(g_);
            }
            filestr << endl;
        }
    }

//    void LoadFromFile(const string s) {
//        FILE* file = fopen((s).c_str(), "r");
//        int res;
//        char ss[5000];
//        map<int, EdgeId> tmp_map;
//        for (auto iter = g.ConstEdgeBegin(); !iter.IsEnd(); ++iter) {
//            tmp_map[g.int_id(*iter)] = *iter;
//        }
//        while (!feof(file)) {
//            int first_id, second_id, first_ind, second_ind;
//            int size;
//            res = fscanf(file, "%d %d\n", &first_id, &size);
//            VERIFY(res == 2);
//            for (int i = 0; i < size; i++) {
//                res = fscanf(file, "%d %d\n", &first_id, &first_ind);
//                VERIFY(res == 2);
//                res = fscanf(file, "%d %d\n", &second_id, &second_ind);
//                VERIFY(res == 2);
//                res = fscanf(file, "%s\n", ss);
//                VERIFY(res == 1);
//                GapDescription<Graph> gap(tmp_map[first_id], tmp_map[second_id], Sequence(ss), first_ind, second_ind);
//                this->AddGap(gap);
//            }
//        }
//    }

    void PadGapStrings() {
        for (const auto& e_gaps : inner_index_) {
            DEBUG("Padding gaps for first edge " << g_.int_id(e_gaps.first));
            PadGapStrings(e_gaps.first);
        }
        PostProcess();
    }
};

template<class Graph>
class PacbioGapCloser {
    typedef typename Graph::EdgeId EdgeId;
    typedef runtime_k::RtSeq Kmer;
    DECL_LOGGER("PacbioGaps");

    Graph& g_;
    //first edge, second edge, weight, seq
    map<EdgeId, map<EdgeId, pair<size_t, string>>> new_edges_;
    int closed_gaps_;
    int not_unique_gaps_;
    int chained_gaps_;
    bool consensus_gap_closing_;
    size_t max_contigs_gap_length_;

    void ConstructConsensus(EdgeId e, const GapStorage<Graph> &storage,
                            map<EdgeId, map<EdgeId, pair<size_t, string>>>& new_edges) const {
        const auto& gaps = get(storage.inner_index(), e);
        auto cl_start = gaps.begin();
        auto iter = gaps.begin();
        size_t cur_len = 0;
        while (iter != gaps.end()) {
            auto next_iter = ++iter;
            cur_len++;
            if (next_iter == gaps.end() || next_iter->end != cl_start->end) {
                if (cur_len >= storage.min_gap_quantity && !storage.IsIgnored(make_pair(cl_start->start, cl_start->end))) {
                    vector<string> gap_variants;

                    for (auto j_iter = cl_start; j_iter != next_iter; j_iter++) {
                        string s = j_iter->gap_seq.str();
                        transform(s.begin(), s.end(), s.begin(), ::toupper);
                        gap_variants.push_back(s);
                    }
                    if (consensus_gap_closing_ || (gap_variants.size() > 0 && gap_variants[0].length() < max_contigs_gap_length_)) {
                        map <EdgeId, pair<size_t, string>> tmp;
                        string tmp_string;
                        string s = g_.EdgeNucls(cl_start->start).Subseq(0, cl_start->edge_gap_start_position).str();
                        if (consensus_gap_closing_) {
                            const ConsensusCore::PoaConsensus *pc = ConsensusCore::PoaConsensus::FindConsensus(
                                    gap_variants,
                                    ConsensusCore::PoaConfig::GLOBAL_ALIGNMENT);
                            tmp_string = pc->Sequence();
                        } else {
                            tmp_string = gap_variants[0];
                            if (gap_variants.size() > 1) {

                                stringstream ss;
                                for (size_t i = 0; i < gap_variants.size(); i++)
                                    ss << gap_variants[i].length() << " ";
                                INFO(gap_variants.size() << " gap closing variant for contigs, lengths: " << ss.str());
                            }
                        }

                        DEBUG("consenus for " << g_.int_id(cl_start->start) << " and " << g_.int_id(cl_start->end) <<
                                                                                          "found: ");
                        DEBUG(tmp_string);
                        s += tmp_string;
                        s += g_.EdgeNucls(cl_start->end).Subseq(cl_start->edge_gap_end_position,
                                                                g_.length(cl_start->end) + g_.k()).str();
                        tmp.insert(make_pair(cl_start->end, make_pair(cur_len, s)));
                        new_edges[cl_start->start] = tmp;
                    } else {
                        INFO ("Skipping gap of size " << gap_variants[0].length() << " multiplicity " << gap_variants.size());
                    }
                }
                cl_start = next_iter;
                cur_len = 0;
            }
        }
    }

    map<EdgeId, map<EdgeId, pair<size_t, string>>> ConstructConsensus(const GapStorage<Graph> &storage,
                                                                      size_t nthreads) const {
        vector<map<EdgeId, map<EdgeId, pair<size_t, string>>>> new_edges_by_thread;
        new_edges_by_thread.resize(nthreads);
        size_t storage_size = storage.size();

        # pragma omp parallel for num_threads(nthreads)
        for (size_t i = 0; i < storage_size; i++) {
            EdgeId e = storage[i];
            size_t thread_num = omp_get_thread_num();
            DEBUG("constructing consenus for first edge " << g_.int_id(e) << " in thread " << thread_num);
            ConstructConsensus(e, storage, new_edges_by_thread[thread_num]);
        }

        map<EdgeId, map<EdgeId, pair<size_t, string>>> new_edges;
        for (size_t i = 0; i < nthreads; i++) {
            new_edges.insert(new_edges_by_thread[i].begin(), new_edges_by_thread[i].end());
        }
        return new_edges;
    }

public:
    PacbioGapCloser(Graph &g, bool consensus_gap, size_t max_contigs_gap_length )
            : g_(g), consensus_gap_closing_(consensus_gap), max_contigs_gap_length_(max_contigs_gap_length) {
        closed_gaps_ = 0;
        not_unique_gaps_ = 0;
        chained_gaps_ = 0;
    }

    map<EdgeId, EdgeId> CloseGapsInGraph(const GapStorage<Graph> &storage, size_t nthreads) {
        new_edges_ = ConstructConsensus(storage, nthreads);

        map<EdgeId, EdgeId> replacement;
        for (auto iter = new_edges_.begin(); iter != new_edges_.end(); ++iter) {
            if (iter->second.size() != 1) {
                DEBUG("non-unique gap!!");
                not_unique_gaps_ ++;
                continue;
            }
            EdgeId first = iter->first;
            EdgeId second = (iter->second.begin()->first);
            if (replacement.find(first) != replacement.end() || replacement.find(second) != replacement.end()) {
                DEBUG("sorry, gap chains are not supported yet");
                chained_gaps_++;
                continue;
            }

            EdgeId first_conj = g_.conjugate(first);
            EdgeId second_conj = g_.conjugate(second);
            size_t first_id = g_.int_id(first);
            size_t second_id = g_.int_id(second);
            size_t first_id_conj = g_.int_id(g_.conjugate(first));
            size_t second_id_conj = g_.int_id(g_.conjugate(second));
            DEBUG("closing gaps between "<< first_id << " " << second_id);
            size_t len_f = g_.length(first);
            size_t len_s = g_.length(second);
            size_t len_sum = iter->second.begin()->second.second.length();
            double cov = (double)g_.length(first) * g_.coverage(first) +  (double)g_.length(second) * g_.coverage(second);

            DEBUG("coverage was " << g_.coverage(first) << " " << g_.coverage(second));

            EdgeId newEdge = g_.AddEdge(g_.EdgeStart(first), g_.EdgeEnd(second), Sequence(iter->second.begin()->second.second));
            if (cov > UINT_MAX * 0.75 ) cov = UINT_MAX*0.75;
            cov /= (double) g_.length(newEdge);
            TRACE(g_.int_id(newEdge));
            int len_split = int(((double) len_f * (double) len_sum) / ((double)len_s + (double)len_f));
            if (len_split == 0) {
                DEBUG(" zero split length, length are:" << len_f <<" " << len_sum <<" " << len_s);
                len_split = 1;
            }
            g_.DeleteEdge(first);
            g_.DeleteEdge(second);
            g_.coverage_index().SetAvgCoverage(newEdge, cov);
            g_.coverage_index().SetAvgCoverage(g_.conjugate(newEdge), cov);
            size_t next_id = g_.int_id(newEdge);
            DEBUG("and new coverage is " << g_.coverage(newEdge));
            closed_gaps_ ++;
            size_t next_id_conj = g_.int_id(g_.conjugate(newEdge));
            TRACE(first_id << " " << second_id << " " << next_id << " " << first_id_conj << " " << second_id_conj << " " << next_id_conj << " ");
            replacement[first] = newEdge;
            replacement[second] = newEdge;
            replacement[first_conj] = g_.conjugate(newEdge);
            replacement[second_conj] = g_.conjugate(newEdge);
        }
        INFO("Closed " << closed_gaps_ << " gaps");
        INFO("Total " << not_unique_gaps_ << " were not closed due to more than one possible pairing");
        INFO("Total " << chained_gaps_ << " were skipped because of gap chains");
        //TODO: chains of gaps!
        return replacement;
    }

    void DumpToFile(const string filename) {
        ofstream filestr(filename);
        for (auto iter = new_edges_.begin(); iter != new_edges_.end(); ++iter) {
            if (iter->second.size() > 1) {
                DEBUG("nontrivial gap closing for edge" <<g_.int_id(iter->first));
            }
            for (auto j_iter = iter->second.begin(); j_iter != iter->second.end(); ++j_iter) {
                filestr << ">" << g_.int_id(iter->first) << "_" << iter->second.size() << "_"
                        << g_.int_id(j_iter->first) << "_" << j_iter->second.first << endl;
                filestr << j_iter->second.second << endl;
            }
        }
    }

};

}
