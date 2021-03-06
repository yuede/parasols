/* vim: set sw=4 sts=4 et foldmethod=syntax : */

#include <subgraph_isomorphism/gb_subgraph_isomorphism.hh>
#include <subgraph_isomorphism/supplemental_graphs.hh>

#include <graph/bit_graph.hh>
#include <graph/template_voodoo.hh>
#include <graph/degree_sort.hh>

#include <algorithm>
#include <limits>
#include <numeric>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/max_cardinality_matching.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/graph_utility.hpp>

using namespace parasols;

namespace
{
    enum class Search
    {
        Aborted,
        Unsatisfiable,
        Satisfiable
    };

    template <unsigned n_words_, bool backjump_, bool double_filter_, int k_, int l_, bool induced_, bool compose_induced_>
    struct SGI :
        SupplementalGraphsMixin<SGI<n_words_, backjump_, double_filter_, k_, l_, induced_, compose_induced_>, n_words_, k_, l_, induced_, compose_induced_>
    {
        using SupplementalGraphsMixin<SGI<n_words_, backjump_, double_filter_, k_, l_, induced_, compose_induced_>, n_words_, k_, l_, induced_, compose_induced_>::build_supplemental_graphs;

        struct Domain
        {
            unsigned v;
            unsigned popcount;
            FixedBitSet<n_words_> values;
        };

        using Domains = std::vector<Domain>;
        using Assignments = std::vector<unsigned>;

        struct DummyFailedVariables
        {
            auto independent_of(const Domains &, const Domains &) -> bool
            {
                return false;
            }

            auto add(unsigned) -> void
            {
            }

            auto add(const DummyFailedVariables &) -> void
            {
            }
        };

        struct RealFailedVariables
        {
            FixedBitSet<n_words_> variables;

            auto independent_of(const Domains & old_domains, const Domains & new_domains) -> bool
            {
                auto vc = variables;
                for (int v = vc.first_set_bit() ; v != -1 ; v = vc.first_set_bit()) {
                    vc.unset(v);

                    auto o = std::find_if(old_domains.begin(), old_domains.end(), [v] (const Domain & d) { return d.v == unsigned(v); });
                    auto n = std::find_if(new_domains.begin(), new_domains.end(), [v] (const Domain & d) { return d.v == unsigned(v); });

                    unsigned opc = (o == old_domains.end() ? 1 : o->popcount);
                    unsigned npc = (n == new_domains.end() ? 1 : n->popcount);
                    if (opc != npc)
                        return false;
                }

                return true;
            }

            auto add(unsigned dv) -> void
            {
                variables.set(dv);
            }

            auto add(const RealFailedVariables & d) -> void
            {
                variables.union_with(d.variables);
            }
        };

        using FailedVariables = typename std::conditional<backjump_, RealFailedVariables, DummyFailedVariables>::type;

        const SubgraphIsomorphismParams & params;
        const bool use_full_all_different, use_cheap_all_different, dom_plus_deg;

        static constexpr int max_graphs = 1 + ((l_ - 1) * k_) + (induced_ ? 1 + (compose_induced_ ? (l_ >= 2 ? 2 : l_) * k_ : 0) : 0);
        std::array<FixedBitGraph<n_words_>, max_graphs> target_graphs;
        std::array<FixedBitGraph<n_words_>, max_graphs> pattern_graphs;

        std::vector<int> pattern_order, target_order, isolated_vertices;
        std::array<int, n_words_ * bits_per_word> pattern_degree_tiebreak;

        unsigned pattern_size, full_pattern_size, target_size;

        SGI(const Graph & target, const Graph & pattern, const SubgraphIsomorphismParams & a, bool fa, bool ca, bool dpd) :
            params(a),
            use_full_all_different(fa),
            use_cheap_all_different(ca),
            dom_plus_deg(dpd),
            target_order(target.size()),
            pattern_size(pattern.size()),
            full_pattern_size(pattern.size()),
            target_size(target.size())
        {
            // strip out isolated vertices in the pattern
            for (unsigned v = 0 ; v < full_pattern_size ; ++v)
                if ((! induced_) && (0 == pattern.degree(v))) {
                    isolated_vertices.push_back(v);
                    --pattern_size;
                }
                else
                    pattern_order.push_back(v);

            // recode pattern to a bit graph
            pattern_graphs.at(0).resize(pattern_size);
            for (unsigned i = 0 ; i < pattern_size ; ++i)
                for (unsigned j = 0 ; j < pattern_size ; ++j)
                    if (pattern.adjacent(pattern_order.at(i), pattern_order.at(j)))
                        pattern_graphs.at(0).add_edge(i, j);

            // determine ordering for target graph vertices
            std::iota(target_order.begin(), target_order.end(), 0);
            degree_sort(target, target_order, false);

            // recode target to a bit graph
            target_graphs.at(0).resize(target_size);
            for (unsigned i = 0 ; i < target_size ; ++i)
                for (unsigned j = 0 ; j < target_size ; ++j)
                    if (target.adjacent(target_order.at(i), target_order.at(j)))
                        target_graphs.at(0).add_edge(i, j);

            for (unsigned j = 0 ; j < pattern_size ; ++j)
                pattern_degree_tiebreak.at(j) = pattern_graphs.at(0).degree(j);
        }

        auto assign(Domains & new_domains, unsigned branch_v, unsigned f_v, int g_end, FailedVariables & failed_variables) -> bool
        {
            // for each remaining domain...
            for (auto & d : new_domains) {
                // all different
                d.values.unset(f_v);

                // for each graph pair...
                for (int g = 0 ; g < g_end ; ++g) {
                    // if we're adjacent...
                    if (pattern_graphs.at(g).adjacent(branch_v, d.v)) {
                        // ...then we can only be mapped to adjacent vertices
                        target_graphs.at(g).intersect_with_row(f_v, d.values);
                    }
                }

                // we might have removed values
                d.popcount = d.values.popcount();
                if (0 == d.popcount) {
                    failed_variables.add(d.v);
                    return false;
                }
            }

            if (use_cheap_all_different) {
                FailedVariables all_different_failed_variables;
                if (! cheap_all_different(new_domains, all_different_failed_variables)) {
                    failed_variables.add(all_different_failed_variables);
                    return false;
                }
            }

            if (use_full_all_different && ! regin_all_different(new_domains)) {
                for (auto & d : new_domains)
                    failed_variables.add(d.v);
                return false;
            }

            return true;
        }

        auto search(
                Assignments & assignments,
                Domains & domains,
                unsigned long long & nodes,
                int g_end) -> std::pair<Search, FailedVariables>
        {
            if (params.abort->load())
                return std::make_pair(Search::Aborted, FailedVariables());

            ++nodes;

            Domain * branch_domain = nullptr;
            if (dom_plus_deg) {
                for (auto & d : domains)
                    if ((! branch_domain) ||
                            d.popcount < branch_domain->popcount ||
                            (d.popcount == branch_domain->popcount && pattern_degree_tiebreak.at(d.v) > pattern_degree_tiebreak.at(branch_domain->v)))
                        branch_domain = &d;
            }
            else {
                for (auto & d : domains)
                    if ((! branch_domain) || d.popcount < branch_domain->popcount || (d.popcount == branch_domain->popcount && d.v < branch_domain->v))
                        branch_domain = &d;
            }

            if (! branch_domain)
                return std::make_pair(Search::Satisfiable, FailedVariables());

            auto remaining = branch_domain->values;
            auto branch_v = branch_domain->v;

            FailedVariables shared_failed_variables;
            shared_failed_variables.add(branch_domain->v);

            for (int f_v = remaining.first_set_bit() ; f_v != -1 ; f_v = remaining.first_set_bit()) {
                remaining.unset(f_v);

                /* try assigning f_v to v */
                assignments.at(branch_v) = f_v;

                /* set up new domains */
                Domains new_domains;
                new_domains.reserve(domains.size() - 1);
                for (auto & d : domains)
                    if (d.v != branch_v)
                        new_domains.push_back(d);

                /* assign and propagate */
                if (! assign(new_domains, branch_v, f_v, g_end, shared_failed_variables))
                    continue;

                auto search_result = search(assignments, new_domains, nodes, g_end);
                switch (search_result.first) {
                    case Search::Satisfiable:    return std::make_pair(Search::Satisfiable, FailedVariables());
                    case Search::Aborted:        return std::make_pair(Search::Aborted, FailedVariables());
                    case Search::Unsatisfiable:  break;
                }

                if (search_result.second.independent_of(domains, new_domains))
                    return search_result;

                shared_failed_variables.add(search_result.second);
            }

            return std::make_pair(Search::Unsatisfiable, shared_failed_variables);
        }

        auto initialise_domains(Domains & domains) -> bool
        {
            unsigned remaining_target_vertices = target_size;
            FixedBitSet<n_words_> allowed_target_vertices;
            allowed_target_vertices.set_up_to(target_size);

            while (true) {
                std::array<std::vector<int>, max_graphs> patterns_degrees;
                std::array<std::vector<int>, max_graphs> targets_degrees;

                for (int g = 0 ; g < max_graphs ; ++g) {
                    patterns_degrees.at(g).resize(pattern_size);
                    targets_degrees.at(g).resize(target_size);
                }

                /* pattern and target degree sequences */
                for (int g = 0 ; g < max_graphs ; ++g) {
                    for (unsigned i = 0 ; i < pattern_size ; ++i)
                        patterns_degrees.at(g).at(i) = pattern_graphs.at(g).degree(i);

                    for (unsigned i = 0 ; i < target_size ; ++i) {
                        FixedBitSet<n_words_> remaining = allowed_target_vertices;
                        target_graphs.at(g).intersect_with_row(i, remaining);
                        targets_degrees.at(g).at(i) = remaining.popcount();
                    }
                }

                /* pattern and target neighbourhood degree sequences */
                std::array<std::array<std::vector<std::vector<int> >, max_graphs>, double_filter_ ? max_graphs : 1> patterns_ndss;
                std::array<std::array<std::vector<std::vector<int> >, max_graphs>, double_filter_ ? max_graphs : 1> targets_ndss;

                for (int g1 = 0 ; g1 < (double_filter_ ? max_graphs : 1) ; ++g1)
                    for (int g2 = 0 ; g2 < max_graphs ; ++g2) {
                        patterns_ndss.at(g1).at(g2).resize(pattern_size);
                        targets_ndss.at(g1).at(g2).resize(target_size);
                    }

                for (int g1 = 0 ; g1 < (double_filter_ ? max_graphs : 1) ; ++g1)
                    for (int g2 = 0 ; g2 < max_graphs ; ++g2) {
                        for (unsigned i = 0 ; i < pattern_size ; ++i) {
                            for (unsigned j = 0 ; j < pattern_size ; ++j) {
                                if (pattern_graphs.at(g1).adjacent(i, j))
                                    patterns_ndss.at(g1).at(g2).at(i).push_back(patterns_degrees.at(g2).at(j));
                            }
                            std::sort(patterns_ndss.at(g1).at(g2).at(i).begin(), patterns_ndss.at(g1).at(g2).at(i).end(), std::greater<int>());
                        }

                        for (unsigned i = 0 ; i < target_size ; ++i) {
                            for (unsigned j = 0 ; j < target_size ; ++j) {
                                if (target_graphs.at(g1).adjacent(i, j))
                                    targets_ndss.at(g1).at(g2).at(i).push_back(targets_degrees.at(g2).at(j));
                            }
                            std::sort(targets_ndss.at(g1).at(g2).at(i).begin(), targets_ndss.at(g1).at(g2).at(i).end(), std::greater<int>());
                        }
                    }

                for (unsigned i = 0 ; i < pattern_size ; ++i) {
                    domains.at(i).v = i;
                    domains.at(i).values.unset_all();

                    for (unsigned j = 0 ; j < target_size ; ++j) {
                        bool ok = true;

                        /* filter disallowed and loops */
                        for (int g = 0 ; g < max_graphs && ok ; ++g) {
                            if (! allowed_target_vertices.test(j))
                                ok = false;
                            else if (pattern_graphs.at(g).adjacent(i, i) && ! target_graphs.at(g).adjacent(j, j))
                                ok = false;
                        }

                        /* filter on NDS size */
                        for (int g1 = 0 ; (g1 < (double_filter_ ? max_graphs : 1)) && ok ; ++g1)
                            for (int g2 = 0 ; g2 < max_graphs && ok ; ++g2)
                                if (targets_ndss.at(g1).at(g2).at(j).size() < patterns_ndss.at(g1).at(g2).at(i).size())
                                    ok = false;

                        /* filter on NDS inclusion */
                        for (int g1 = 0 ; (g1 < (double_filter_ ? max_graphs : 1)) && ok ; ++g1)
                            for (int g2 = 0 ; g2 < max_graphs && ok ; ++g2)
                                for (unsigned x = 0 ; ok && x < patterns_ndss.at(g1).at(g2).at(i).size() ; ++x)
                                    if (targets_ndss.at(g1).at(g2).at(j).at(x) < patterns_ndss.at(g1).at(g2).at(i).at(x))
                                        ok = false;

                        if (ok)
                            domains.at(i).values.set(j);
                    }

                    domains.at(i).popcount = domains.at(i).values.popcount();
                }

                FixedBitSet<n_words_> domains_union;
                for (auto & d : domains)
                    domains_union.union_with(d.values);

                unsigned domains_union_popcount = domains_union.popcount();
                if (domains_union_popcount < unsigned(pattern_size)) {
                    return false;
                }
                else if (domains_union_popcount == remaining_target_vertices)
                    break;

                allowed_target_vertices.intersect_with(domains_union);
                remaining_target_vertices = allowed_target_vertices.popcount();
            }

            return true;
        }

        auto cheap_all_different(Domains & domains, FailedVariables & failed_variables) -> bool
        {
            // pick domains smallest first, with tiebreaking
            std::array<int, n_words_ * bits_per_word> domains_order;
            std::iota(domains_order.begin(), domains_order.begin() + domains.size(), 0);

            std::sort(domains_order.begin(), domains_order.begin() + domains.size(),
                    [&] (int a, int b) {
                    return (domains.at(a).popcount < domains.at(b).popcount) ||
                    (domains.at(a).popcount == domains.at(b).popcount && pattern_degree_tiebreak.at(domains.at(a).v) > pattern_degree_tiebreak.at(domains.at(b).v));
                    });

            // counting all-different
            FixedBitSet<n_words_> domains_so_far, hall;
            unsigned neighbours_so_far = 0;

            for (int i = 0, i_end = domains.size() ; i != i_end ; ++i) {
                auto & d = domains.at(domains_order.at(i));

                failed_variables.add(d.v);

                d.values.intersect_with_complement(hall);
                d.popcount = d.values.popcount();

                if (0 == d.popcount)
                    return false;

                domains_so_far.union_with(d.values);
                ++neighbours_so_far;

                unsigned domains_so_far_popcount = domains_so_far.popcount();
                if (domains_so_far_popcount < neighbours_so_far) {
                    return false;
                }
                else if (domains_so_far_popcount == neighbours_so_far) {
                    neighbours_so_far = 0;
                    hall.union_with(domains_so_far);
                    domains_so_far.unset_all();
                }
            }

            return true;
        }

        auto regin_all_different(Domains & domains) -> bool
        {
            boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS> match(domains.size() + target_size);

            unsigned consider = 0;
            for (unsigned i = 0 ; i < domains.size() ; ++i) {
                if (domains.at(i).values.popcount() < domains.size())
                    ++consider;

                for (unsigned j = 0 ; j < target_size ; ++j) {
                    if (domains.at(i).values.test(j))
                        boost::add_edge(i, domains.size() + j, match);
                }
            }

            if (0 == consider)
                return true;

            std::vector<boost::graph_traits<decltype(match)>::vertex_descriptor> mate(domains.size() + target_size);
            boost::edmonds_maximum_cardinality_matching(match, &mate.at(0));

            std::set<int> free;
            for (unsigned j = 0 ; j < target_size ; ++j)
                free.insert(domains.size() + j);

            unsigned count = 0;
            for (unsigned i = 0 ; i < domains.size() ; ++i)
                if (mate.at(i) != boost::graph_traits<decltype(match)>::null_vertex()) {
                    ++count;
                    free.erase(mate.at(i));
                }

            if (count != unsigned(domains.size()))
                return false;

            boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS> match_o(domains.size() + target_size);
            std::set<std::pair<unsigned, unsigned> > unused;
            for (unsigned i = 0 ; i < domains.size() ; ++i) {
                for (unsigned j = 0 ; j < target_size ; ++j) {
                    if (domains.at(i).values.test(j)) {
                        unused.emplace(i, j);
                        if (mate.at(i) == unsigned(j + domains.size()))
                            boost::add_edge(i, domains.size() + j, match_o);
                        else
                            boost::add_edge(domains.size() + j, i, match_o);
                    }
                }
            }

            std::set<int> pending = free, seen;
            while (! pending.empty()) {
                unsigned v = *pending.begin();
                pending.erase(pending.begin());
                if (! seen.count(v)) {
                    seen.insert(v);
                    auto w = boost::adjacent_vertices(v, match_o);
                    for ( ; w.first != w.second ; ++w.first) {
                        if (*w.first >= unsigned(domains.size()))
                            unused.erase(std::make_pair(v, *w.first - domains.size()));
                        else
                            unused.erase(std::make_pair(*w.first, v - domains.size()));
                        pending.insert(*w.first);
                    }
                }
            }

            std::vector<int> component(num_vertices(match_o)), discover_time(num_vertices(match_o));
            std::vector<boost::default_color_type> color(num_vertices(match_o));
            std::vector<boost::graph_traits<decltype(match_o)>::vertex_descriptor> root(num_vertices(match_o));
            boost::strong_components(match_o,
                    make_iterator_property_map(component.begin(), get(boost::vertex_index, match_o)),
                    root_map(make_iterator_property_map(root.begin(), get(boost::vertex_index, match_o))).
                    color_map(make_iterator_property_map(color.begin(), get(boost::vertex_index, match_o))).
                    discover_time_map(make_iterator_property_map(discover_time.begin(), get(boost::vertex_index, match_o))));

            for (auto e = unused.begin(), e_end = unused.end() ; e != e_end ; ) {
                if (component.at(e->first) == component.at(e->second + domains.size()))
                    unused.erase(e++);
                else
                    ++e;
            }

            for (auto & u : unused)
                if (mate.at(u.first) != u.second + domains.size())
                    domains.at(u.first).values.unset(u.second);

            return true;
        }

        auto prepare_for_search(Domains & domains) -> void
        {
            for (auto & d : domains)
                d.popcount = d.values.popcount();
        }

        auto save_result(Assignments & assignments, SubgraphIsomorphismResult & result) -> void
        {
            for (unsigned v = 0 ; v < pattern_size ; ++v)
                result.isomorphism.emplace(pattern_order.at(v), target_order.at(assignments.at(v)));

            int t = 0;
            for (auto & v : isolated_vertices) {
                while (result.isomorphism.end() != std::find_if(result.isomorphism.begin(), result.isomorphism.end(),
                            [&t] (const std::pair<int, int> & p) { return p.second == t; }))
                        ++t;
                result.isomorphism.emplace(v, t);
            }
        }

        auto run() -> SubgraphIsomorphismResult
        {
            SubgraphIsomorphismResult result;

            if (full_pattern_size > target_size) {
                /* some of our fixed size data structures will throw a hissy
                 * fit. check this early. */
                return result;
            }

            build_supplemental_graphs();

            Domains domains(pattern_size);

            if (! initialise_domains(domains))
                return result;

            FailedVariables dummy_failed_variables;
            if (! cheap_all_different(domains, dummy_failed_variables))
                return result;

            if (use_full_all_different && ! regin_all_different(domains))
                return result;

            prepare_for_search(domains);

            Assignments assignments(pattern_size, std::numeric_limits<unsigned>::max());
            switch (search(assignments, domains, result.nodes, max_graphs).first) {
                case Search::Satisfiable:
                    save_result(assignments, result);
                    break;

                case Search::Unsatisfiable:
                    break;

                case Search::Aborted:
                    break;
            }

            return result;
        }
    };

    template <template <unsigned, bool, bool, int, int, bool, bool> class SGI_, bool b_, bool d_, int n_, int m_, bool induced_, bool compose_induced_>
    struct Apply
    {
        template <unsigned size_, typename> using Type = SGI_<size_, b_, d_, n_, m_, induced_, compose_induced_>;
    };
}

auto parasols::gb_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, false, false, 3, 3, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
    else
        return select_graph_size<Apply<SGI, false, false, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
}

auto parasols::gbbj_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, false, 3, 3, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
    else
        return select_graph_size<Apply<SGI, true, false, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
}

auto parasols::gbbj_nocompose_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, false, 3, 3, true, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
    else
        return select_graph_size<Apply<SGI, true, false, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
}

auto parasols::gbbj_nosup_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, false, 1, 1, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
    else
        return select_graph_size<Apply<SGI, true, false, 1, 1, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
}

auto parasols::gbbj_nocad_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, false, 3, 3, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, false, true);
    else
        return select_graph_size<Apply<SGI, true, false, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, false, true);
}

auto parasols::gbbj_fad_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, false, 3, 3, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, true, true, true);
    else
        return select_graph_size<Apply<SGI, true, false, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, true, true, true);
}

auto parasols::dgbbj_subgraph_isomorphism(const std::pair<Graph, Graph> & graphs, const SubgraphIsomorphismParams & params) -> SubgraphIsomorphismResult
{
    if (graphs.first.size() > graphs.second.size())
        return SubgraphIsomorphismResult{ };
    if (params.induced)
        return select_graph_size<Apply<SGI, true, true, 3, 3, true, true>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
    else
        return select_graph_size<Apply<SGI, true, true, 3, 3, false, false>::template Type, SubgraphIsomorphismResult>(
                AllGraphSizes(), graphs.second, graphs.first, params, false, true, true);
}

