#include <set>

#include "polymorphisms_detection.hpp"

namespace debruijn_graph {

PolymorphismBubble::PolymorphismBubble(VertexId source, VertexId sink)
        : source(source),
          sink(sink) {}

void PolymorphismsDetection::run(conj_graph_pack &gp, const char*) {
    INFO("Polymorphisms detection started with " << gp.g.size() << " vertices");
    INFO("Detecting simple bubbles");
    std::vector<PolymorphismBubble> simple_bubbles;
    for (VertexId source : gp.g) {
        if (gp.g.IsDeadEnd(source)) {
            continue;
        }
        VertexId sink = gp.g.EdgeEnd(*gp.g.OutgoingEdges(source).begin());
        size_t edges_between_cnt = gp.g.GetEdgesBetween(source, sink).size();
        if (edges_between_cnt >= 2 &&
                edges_between_cnt == gp.g.OutgoingEdgeCount(source) &&
                edges_between_cnt == gp.g.IncomingEdgeCount(sink)) {
            simple_bubbles.push_back(PolymorphismBubble(source, sink));
        }
    }
    INFO(simple_bubbles.size() << " simple bubbles found");
    INFO("Polymorphisms detection ended");
}

} // debruijn_graph
