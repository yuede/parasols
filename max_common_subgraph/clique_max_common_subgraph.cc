/* vim: set sw=4 sts=4 et foldmethod=syntax : */

#include <max_common_subgraph/clique_max_common_subgraph.hh>
#include <graph/product.hh>

#include <algorithm>

using namespace parasols;

auto parasols::clique_max_common_subgraph(
        const std::pair<Graph, Graph> & graphs,
        const MaxCommonSubgraphParams & params) -> MaxCommonSubgraphResult
{
    MaxCliqueParams clique_params;
    clique_params.initial_bound = params.initial_bound;
    clique_params.stop_after_finding = params.stop_after_finding;
    clique_params.n_threads = params.n_threads;
    clique_params.print_incumbents = params.print_incumbents;
    clique_params.start_time = params.start_time;
    clique_params.order_function = params.order_function;
    clique_params.abort = params.abort;

    clique_params.stop_after_finding = std::min({ clique_params.stop_after_finding, unsigned(graphs.first.size()), unsigned(graphs.second.size()) });
    if (params.subgraph_isomorphism)
        clique_params.initial_bound = graphs.first.size() - 1;

    Graph product = params.subgraph_isomorphism ?
        subgraph_modular_product(graphs.first, graphs.second) :
        modular_product(graphs.first, graphs.second);

    auto clique_result = params.max_clique_algorithm(product, clique_params);

    MaxCommonSubgraphResult result;
    result.size = clique_result.size;
    result.nodes = clique_result.nodes;
    result.times = clique_result.times;

    for (auto & v : clique_result.members)
        result.isomorphism.emplace(unproduct(graphs.first, graphs.second, v));

    return result;
}

