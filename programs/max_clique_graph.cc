/* vim: set sw=4 sts=4 et foldmethod=syntax : */

#include <max_clique/max_clique_params.hh>
#include <max_clique/max_clique_result.hh>

#include <max_clique/naive_max_clique.hh>
#include <max_clique/mcsa1_max_clique.hh>
#include <max_clique/bmcsa_max_clique.hh>
#include <max_clique/cco_max_clique.hh>

#include <boost/program_options.hpp>

#include <iostream>
#include <iomanip>
#include <exception>
#include <algorithm>
#include <random>
#include <cstdlib>

using namespace parasols;
namespace po = boost::program_options;

std::mt19937 rnd;

namespace
{
    auto algorithms = {
        std::make_tuple( std::string{ "naive" },      naive_max_clique),
        std::make_tuple( std::string{ "mcsa1" },      mcsa1_max_clique),
        std::make_tuple( std::string{ "bmcsa1" },     bmcsa_max_clique<MaxCliqueOrder::Degree>),
        std::make_tuple( std::string{ "bmcsa2" },     bmcsa_max_clique<MaxCliqueOrder::MinWidth>),
        std::make_tuple( std::string{ "bmcsa3" },     bmcsa_max_clique<MaxCliqueOrder::ExDegree>),
        std::make_tuple( std::string{ "bmcsar" },     bmcsa_max_clique<MaxCliqueOrder::DynExDegree>),
        std::make_tuple( std::string{ "ccon1" },      cco_max_clique<CCOPermutations::None,   MaxCliqueOrder::Degree>),
        std::make_tuple( std::string{ "ccod11" },     cco_max_clique<CCOPermutations::Defer1, MaxCliqueOrder::Degree>),
        std::make_tuple( std::string{ "ccod21" },     cco_max_clique<CCOPermutations::Defer2, MaxCliqueOrder::Degree>),
        std::make_tuple( std::string{ "ccos1" },      cco_max_clique<CCOPermutations::Sort,   MaxCliqueOrder::Degree>),
        std::make_tuple( std::string{ "ccon2" },      cco_max_clique<CCOPermutations::None,   MaxCliqueOrder::MinWidth>),
        std::make_tuple( std::string{ "ccod12" },     cco_max_clique<CCOPermutations::Defer1, MaxCliqueOrder::MinWidth>),
        std::make_tuple( std::string{ "ccod22" },     cco_max_clique<CCOPermutations::Defer2, MaxCliqueOrder::MinWidth>),
        std::make_tuple( std::string{ "ccos2" },      cco_max_clique<CCOPermutations::Sort,   MaxCliqueOrder::MinWidth>),
        std::make_tuple( std::string{ "ccon3" },      cco_max_clique<CCOPermutations::None,   MaxCliqueOrder::ExDegree>),
        std::make_tuple( std::string{ "ccod13" },     cco_max_clique<CCOPermutations::Defer1, MaxCliqueOrder::ExDegree>),
        std::make_tuple( std::string{ "ccod23" },     cco_max_clique<CCOPermutations::Defer2, MaxCliqueOrder::ExDegree>),
        std::make_tuple( std::string{ "ccos3" },      cco_max_clique<CCOPermutations::Sort,   MaxCliqueOrder::ExDegree>),
        std::make_tuple( std::string{ "cconr" },      cco_max_clique<CCOPermutations::None,   MaxCliqueOrder::DynExDegree>),
        std::make_tuple( std::string{ "ccod1r" },     cco_max_clique<CCOPermutations::Defer1, MaxCliqueOrder::DynExDegree>),
        std::make_tuple( std::string{ "ccod2r" },     cco_max_clique<CCOPermutations::Defer2, MaxCliqueOrder::DynExDegree>),
        std::make_tuple( std::string{ "ccosr" },      cco_max_clique<CCOPermutations::Sort,   MaxCliqueOrder::DynExDegree>)
    };
}

void table(int size, int samples, const std::function<MaxCliqueResult (const Graph &, const MaxCliqueParams &)> & algorithm)
{
    std::cout << "# probability omega_average nodes_average time_average "
        << "find_omega_average find_nodes_average find_time_average" << std::endl;

    for (int p = 1 ; p < 100 ; ++p) {
        double omega_average = 0, find_omega_average = 0;
        double nodes_average = 0, find_nodes_average = 0;
        double time_average = 0, find_time_average = 0;

        for (int n = 0 ; n < samples ; ++n) {
            Graph graph(size, false);

            std::uniform_real_distribution<double> dist(0.0, 1.0);
            for (int e = 0 ; e < size ; ++e)
                for (int f = e + 1 ; f < size ; ++f)
                    if (dist(rnd) <= (double(p) / 100.0))
                        graph.add_edge(e, f);

            unsigned omega;
            {
                MaxCliqueParams params;
                params.original_graph = &graph;
                params.abort.store(false);

                params.start_time = std::chrono::steady_clock::now();

                MaxCliqueResult result = algorithm(graph, params);

                auto overall_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - params.start_time);

                omega_average += double(result.size) / double(samples);
                nodes_average += double(result.nodes) / double(samples);
                time_average += double(overall_time.count()) / double(samples);
                omega = result.size;
            }

            {
                MaxCliqueParams params;
                params.original_graph = &graph;
                params.abort.store(false);
                params.stop_after_finding = omega;

                params.start_time = std::chrono::steady_clock::now();

                MaxCliqueResult result = algorithm(graph, params);

                auto overall_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - params.start_time);

                find_omega_average += double(result.size) / double(samples);
                find_nodes_average += double(result.nodes) / double(samples);
                find_time_average += double(overall_time.count()) / double(samples);
            }
        }

        std::cout
            << (double(p) / 100.0)
            << " " << omega_average << " " << nodes_average << " " << time_average
            << " " << find_omega_average << " " << find_nodes_average << " " << find_time_average
            << std::endl;
    }
}

auto main(int argc, char * argv[]) -> int
{
    try {
        po::options_description display_options{ "Program options" };
        display_options.add_options()
            ("help",                                  "Display help information")
            ;

        po::options_description all_options{ "All options" };
        all_options.add(display_options);

        all_options.add_options()
            ("algorithm",   po::value<std::string>(),  "Algorithm")
            ("size",        po::value<int>(),          "Size of graph")
            ("samples",     po::value<int>(),          "Sample size")
            ;

        po::positional_options_description positional_options;
        positional_options
            .add("algorithm",    1)
            .add("size",         1)
            .add("samples",      1)
            ;

        po::variables_map options_vars;
        po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(positional_options)
                .run(), options_vars);
        po::notify(options_vars);

        /* --help? Show a message, and exit. */
        if (options_vars.count("help")) {
            std::cout << "Usage: " << argv[0] << " [options] algorithm size samples" << std::endl;
            std::cout << std::endl;
            std::cout << display_options << std::endl;
            return EXIT_SUCCESS;
        }

        /* No values specified? Show a message and exit. */
        if (! options_vars.count("algorithm") || ! options_vars.count("size") || ! options_vars.count("samples")) {
            std::cout << "Usage: " << argv[0] << " [options] algorithm size samples" << std::endl;
            return EXIT_FAILURE;
        }

        int size = options_vars["size"].as<int>();
        int samples = options_vars["samples"].as<int>();

        /* Turn an algorithm string name into a runnable function. */
        auto algorithm = algorithms.begin(), algorithm_end = algorithms.end();
        for ( ; algorithm != algorithm_end ; ++algorithm)
            if (std::get<0>(*algorithm) == options_vars["algorithm"].as<std::string>())
                break;

        if (algorithm == algorithm_end) {
            std::cerr << "Unknown algorithm " << options_vars["algorithm"].as<std::string>() << ", choose from:";
            for (auto a : algorithms)
                std::cerr << " " << std::get<0>(a);
            std::cerr << std::endl;
            return EXIT_FAILURE;
        }

        table(size, samples, std::get<1>(*algorithm));

        return EXIT_SUCCESS;
    }
    catch (const po::error & e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Try " << argv[0] << " --help" << std::endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception & e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

