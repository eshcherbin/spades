#pragma once

#include "../environment.hpp"
#include "../command.hpp"
#include "../command_type.hpp"
#include "../errors.hpp"
#include "omni/omni_utils.hpp"

namespace online_visualization {
    class PrintContigsStatsCommand : public LocalCommand {
        //typedef vector<EdgeId> Path;
        
        private:
            mutable bool ext_output;

            //void info(string text) const {
                //cout << text << endl;   
            //}
            
            //void debug(string text) const {
                //if (ext_output)
                    //info(text);
            //}

            vector<EdgeId> TryCloseGap(const Graph& graph, VertexId v1, VertexId v2) const {
                if (v1 == v2)
                    return vector<EdgeId>();
                TRACE("Trying to close gap between v1 =" << graph.int_id(v1) << " and v2 =" << graph.int_id(v2));
                PathStorageCallback<Graph> path_storage(graph);
            
                //  todo reduce value after investigation
                PathProcessor<Graph> path_processor(graph, 0, 50, v1, v2, path_storage);
                path_processor.Process();

                if (path_storage.size() == 0) {
                    TRACE("Failed to find closing path");
                    return vector<EdgeId>();
                } else if (path_storage.size() == 1) {
                    TRACE("Unique closing path found");
                } else {
                    TRACE("Several closing paths found, first chosen");
                }
                vector<EdgeId> answer = path_storage.paths().front();
                TRACE("Gap closed");
                TRACE("Cumulative closure length is " 
                        << CummulativeLength(graph, answer));
                return answer;
            }

            vector<EdgeId> TryFixPath(Environment& curr_env, const vector<EdgeId>& edges) const {
                vector<EdgeId> answer;
                if (edges.empty()) {
                    //  WARN("Mapping path was empty");
                    return vector<EdgeId>();
                }
                //  VERIFY(edges.size() > 0);
                answer.push_back(edges[0]);
                for (size_t i = 1; i < edges.size(); ++i) {
                    vector<EdgeId> closure = TryCloseGap(curr_env.graph(), curr_env.graph().EdgeEnd(edges[i - 1]), curr_env.graph().EdgeStart(edges[i]));
                    answer.insert(answer.end(), closure.begin(), closure.end());
                    answer.push_back(edges[i]);
                }
                return answer;
            }

            Path<EdgeId> TryFixPath(Environment& curr_env, const Path<EdgeId>& path) const {
                return Path<EdgeId>(TryFixPath(curr_env, path.sequence()), path.start_pos(), path.end_pos());
            }

        private:

            bool ProcessContig(Environment& curr_env, const Sequence& contig, const MappingPath<EdgeId>& genome_path, const string& contig_name) const {
                debug(ext_output, " Checking the contig " << contig_name);
                debug(ext_output, " Length " << contig.size());
                const Path<EdgeId>& genome_path_completed = TryFixPath(curr_env, genome_path.simple_path());
                const MappingPath<EdgeId>& contig_path = curr_env.mapper().MapSequence(contig);
                if (contig_path.size() == 0) {
                    debug(ext_output, "Contig could not be aligned at all!");
                    return false;
                }
                bool found = false;
                EdgeId first_edge = contig_path[0].first;
                for (size_t i = 0; i < genome_path_completed.size(); ++i) {
                    TRACE(" i-th edge of the genome " << genome_path_completed[i]);
                    if (genome_path_completed[i] == first_edge) {
                        found = true;
                        for (size_t j = 1; j < contig_path.size(); ++j) {
                            if (genome_path_completed[i + j] != contig_path[j].first) {
                                debug(ext_output, " Break in the edge " << curr_env.graph().int_id(contig_path[j].first));
                                return false;
                            }
                        }
                    }
                }
                if (!found) {
                    debug(ext_output, " First edge " << curr_env.graph().int_id(first_edge) << " was not found");
                    return false;
                } else {
                    debug(ext_output, " No misassemblies");
                    return true;
                }
            }

        protected:
            size_t MinArgNumber() const {
                return 1;   
            }
            
            bool CheckCorrectness(const vector<string>& args) const {
                if (!CheckEnoughArguments(args))
                    return false;

                const string& file = args[1];
                if (!CheckFileExists(file))
                    return false;
                
                return true;
            }
 
        public:
            string Usage() const {
                string answer;
                answer = answer + "Command `print_contigs_stats` \n" + 
                                " Usage:\n" + 
                                "> print_contigs_stats <contigs_file> [--stats]\n" + 
                                " Shows the results of aligning the contigs in the <contigs_file> to the current DB graph. \n" +
                                " --stats allows to see the details.";
                return answer;
            }

            PrintContigsStatsCommand() : LocalCommand(CommandType::print_contigs_stats)
            {
            }

            void Execute(Environment& curr_env, const ArgumentList& arg_list) const {
                const vector<string>& args = arg_list.GetAllArguments();
                if (!CheckCorrectness(args))
                    return;

                string file = args[1];
                ext_output = (arg_list["stats"] == "true");
                
                TRACE("Printing stats " << ext_output);
                
                io::Reader irs(file);
                
                const Sequence& genome = curr_env.genome();

                const MappingPath<EdgeId>& genome_path = curr_env.mapper().MapSequence(genome);

                while (!irs.eof()) {
                    io::SingleRead read;
                    irs >> read;
                    if (read.IsValid()) {
                        const Sequence& contig = read.sequence();
                        bool result = false;
                        result = result | ProcessContig(curr_env, contig, genome_path, "CONTIG_" + read.name());
                        result = result | ProcessContig(curr_env, !contig, genome_path, "CONTIG_" + read.name() + "_RC");
                        if (result) {
                            info(" contig " + read.name() + " is OKAY");
                        }
                        else 
                            info(" contig " + read.name() + " is MISASSEMBLED");
                    }
                }
            }
        private:
            DECL_LOGGER("PrintContigsStats");
    };
}