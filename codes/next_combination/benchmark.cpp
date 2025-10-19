
#include <algorithm>
#include <compare>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#include "tracked.hpp"

#include "impl/biowpn.hpp"
#include "impl/bronnimann.hpp"
#include "impl/hinnant.hpp"

void reset_tracked_stats() {
    using namespace tracked_stats;
    value_comparisons = 0;
    value_swaps = 0;
    iter_comparisons = 0;
    iter_increments = 0;
}

static void report_tracked_stats() {
    using namespace tracked_stats;
    std::cout << "value comparisons: " << value_comparisons << '\n';
    std::cout << "value swaps:       " << value_swaps << '\n';
    std::cout << "iter comparisons:  " << iter_comparisons << '\n';
    std::cout << "iter increments:   " << iter_increments << '\n';
}

void benchmark_next_combination(size_t n, auto next_combination_fn) {
    reset_tracked_stats();

    for (std::size_t r = 0; r <= n; ++r) {
        std::vector<tracked<size_t>> data;
        for (std::size_t i = 0; i < n; ++i) {
            data.push_back({i});
        }
        while (next_combination_fn(tracked_iterator(data.begin()),
                                   tracked_iterator(data.begin() + r),
                                   tracked_iterator(data.end())))
            ;
    }

    report_tracked_stats();
}

void benchmark_for_each_combination(size_t n, auto for_each_combination_fn) {
    reset_tracked_stats();

    for (std::size_t r = 0; r <= n; ++r) {
        std::vector<tracked<size_t>> data;
        for (std::size_t i = 0; i < n; ++i) {
            data.push_back({i});
        }
        for_each_combination_fn(
            tracked_iterator(data.begin()), tracked_iterator(data.begin() + r),
            tracked_iterator(data.end()), [](auto, auto) { return false; });
    }

    report_tracked_stats();
}

void benchmark_biowpn(size_t n) {
    std::cout << "biowpn:\n";
    benchmark_next_combination(n, [](auto first, auto mid, auto last) {
        return biowpn::next_combination(first, mid, last);
    });
    std::cout << "\n";
}

void benchmark_bronnimann(size_t n) {
    std::cout << "bronnimann:\n";
    benchmark_next_combination(n, [](auto first, auto mid, auto last) {
        return bronnimann::next_combination(first, mid, last);
    });
    std::cout << "\n";
}

void benchmark_hinnant(size_t n) {
    std::cout << "hinnant:\n";
    benchmark_for_each_combination(
        n, [](auto first, auto mid, auto last, auto f) {
            for_each_combination(first, mid, last, f);
        });
    std::cout << "\n";
}

int main() {
    benchmark_bronnimann(20);
    benchmark_hinnant(20);
    benchmark_biowpn(20);
}
