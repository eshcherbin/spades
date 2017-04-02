#pragma once

#include <vector>
#include <unordered_map>
#include <boost/optional.hpp>
#include <pipeline/stage.hpp>

namespace debruijn_graph {

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
};

} // debruijn_graph

