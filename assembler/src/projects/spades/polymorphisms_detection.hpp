#pragma once

#include <vector>
#include <unordered_map>
#include <boost/optional.hpp>
#include <pipeline/stage.hpp>

namespace debruijn_graph {

namespace vertex_traversal {

enum VertexVisitStatus {
    NOT_SEEN,
    SEEN,
    VISITED,
};

struct VertexTraversalStatus {
    size_t indeg;
    VertexVisitStatus visit_status;
    size_t last_reset;

    VertexTraversalStatus();
};

} // vertex_traversal

struct PolymorphismBubble {
    VertexId source;
    VertexId sink;

    PolymorphismBubble(VertexId source, VertexId sink);
};

class PolymorphismsDetection : public spades::AssemblyStage {
public:
    PolymorphismsDetection()
            : AssemblyStage("Polymorphisms Detection", "polymorphisms_detection") { }

    void run(conj_graph_pack &gp, const char*);

private:
    std::unordered_map<VertexId, vertex_traversal::VertexTraversalStatus> vertices_status_;

    boost::optional<VertexId> FindSink(conj_graph_pack &gp, VertexId source);
};

} // debruijn_graph

