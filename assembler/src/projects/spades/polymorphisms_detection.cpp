#include <set>
#include <assembly_graph/graph_support/contig_output.hpp>

#include "polymorphisms_detection.hpp"

namespace debruijn_graph {

namespace vertex_traversal {
vertex_traversal::VertexTraversalStatus::VertexTraversalStatus()
        : indeg(0),
          visit_status(VertexVisitStatus::NOT_SEEN),
          last_reset((size_t) -1) {}

} // vertex_traversal

using namespace vertex_traversal;

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
    OutputBubbles(gp, simple_bubbles, "simple");
    INFO("Detecting all bubbles");
    vertices_status_.reserve(gp.g.size());
    std::vector<PolymorphismBubble> all_bubbles;
    for (VertexId source : gp.g) {
        if (boost::optional<VertexId> sink = FindSink(gp, source)) {
            if (gp.g.GetEdgesBetween(source, *sink).size() != 1) {
                all_bubbles.push_back(PolymorphismBubble(source, *sink));
            }
        }
    }
    INFO(all_bubbles.size() << " bubbles found");
    OutputBubbles(gp, all_bubbles, "all");
    INFO("Polymorphisms detection ended");
}

boost::optional<VertexId> PolymorphismsDetection::FindSink(conj_graph_pack &gp, VertexId source) {
    size_t cur_reset_id = source.int_id();
    VertexTraversalStatus& source_status = vertices_status_[source];
    source_status.last_reset = cur_reset_id;
    source_status.indeg = gp.g.IncomingEdgeCount(source);
    source_status.visit_status = VertexVisitStatus::SEEN;
    int seen_cnt = 1;
    std::queue<VertexId> queue;
    queue.push(source);
    while (!queue.empty()) {
        VertexId v = queue.front();
        queue.pop();
        VertexTraversalStatus &v_status = vertices_status_[v];
        v_status.visit_status = VertexVisitStatus::VISITED;
        --seen_cnt;
        if (gp.g.IsDeadEnd(v)) {
            return boost::none;
        }
        for (EdgeId edge : gp.g.OutgoingEdges(v)) {
            VertexId u = gp.g.EdgeEnd(edge);
            if (u == source) {
                return boost::none;
            }
            VertexTraversalStatus &u_status = vertices_status_[u];
            if (u_status.last_reset != cur_reset_id) {
                u_status.visit_status = VertexVisitStatus::NOT_SEEN;
                u_status.indeg = gp.g.IncomingEdgeCount(u);
                u_status.last_reset = cur_reset_id;
            }
            if (u_status.visit_status == VertexVisitStatus::NOT_SEEN) {
                ++seen_cnt;
            }
            u_status.visit_status = VertexVisitStatus::SEEN;
            --u_status.indeg;
            if (u_status.indeg == 0) {
                queue.push(u);
            }
        }
        if (queue.size() == 1 && seen_cnt == 1) {
            VertexId sink = queue.front();
            if (gp.g.GetEdgesBetween(sink, source).empty()) {
                return sink;
            } else {
                return boost::none;
            }
        }
    }
    return boost::none;
}

template <class BubblesContainer>
void PolymorphismsDetection::OutputBubbles(conj_graph_pack& gp, const BubblesContainer& bubbles,
                                           const string& bubbles_name) const {
    INFO("Collecting " << bubbles_name << " bubbles into a single graph component");
    std::set<EdgeId> bubbles_edges;
    for (const auto& bubble : bubbles) {
        CollectBubble(gp, bubble.source, bubble.sink, bubbles_edges);
    }
    GraphComponent<conj_graph_pack::graph_t> bubbles_component =
            GraphComponent<conj_graph_pack::graph_t>::FromEdges(gp.g, bubbles_edges, false,
                                                                bubbles_name + "_bubbles");
    INFO("Collected " << bubbles_edges.size() << " edges");
    path_extend::PathContainer paths;
    OutputComponentToGFA(bubbles_component, paths, cfg::get().output_dir + bubbles_component.name());
}

void PolymorphismsDetection::CollectBubble(conj_graph_pack& gp, VertexId source, VertexId sink,
                                           std::set<EdgeId>& result) const {
    std::queue<VertexId> queue;
    std::set<VertexId> seen;
    queue.push(source);
    seen.insert(source);
    while (!queue.empty()) {
        VertexId v = queue.front();
        queue.pop();
        if (v == sink) {
            continue;
        }
        for (EdgeId edge : gp.g.OutgoingEdges(v)) {
            result.insert(edge);
            VertexId u = gp.g.EdgeEnd(edge);
            if (seen.count(u) == 0) {
                seen.insert(u);
                queue.push(u);
            }
        }
    }
    // add some entrance and exit edges for GFA to look nice
    if (!gp.g.IsDeadStart(source)) {
        result.insert(*gp.g.IncomingEdges(source).begin());
    }
    if (!gp.g.IsDeadEnd(sink)) {
        result.insert(*gp.g.OutgoingEdges(sink).begin());
    }
}

} // debruijn_graph
