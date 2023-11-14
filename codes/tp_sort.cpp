#include <algorithm>
#include <iostream>
#include <iterator>
#include <queue>
#include <string>
#include <vector>

/// Reorder [first, last) based on indices from [order, order + (last - first))
/// Based on https://stackoverflow.com/a/22183350
template <std::random_access_iterator I, class S, std::random_access_iterator O>
void reorder(I first, S last, O order) {
    auto n = static_cast<std::size_t>(std::ranges::distance(first, last));
    for (std::size_t i = 0; i < n; ++i) {
        if (i != order[i]) {
            auto temp = std::move(first[i]);
            std::size_t j = i;
            for (std::size_t k = order[j]; k != i; j = k, k = order[j]) {
                first[j] = std::move(first[k]);
                order[j] = j;
            }
            first[j] = std::move(temp);
            order[j] = j;
        }
    }
}

/// topological sort, Kahn's algorithm
template <std::random_access_iterator I, class S, class F>
void topological_sort(I first, S last, F edge) {
    std::size_t n = std::ranges::distance(first, last);
    std::vector<std::size_t> in_degree(n);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            in_degree[i] += bool(edge(first[j], first[i]));
        }
    }

    // [s_first, s_last) contain the sources of the sub-graph [s_last, last)
    // It's also a moving FIFO queue.
    auto s_first = first;
    auto s_last = s_first;

    // initialize the queue
    for (std::size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            std::swap(first[i], *s_last);
            std::swap(in_degree[i], in_degree[s_last - first]);
            ++s_last;
        }
    }

    for (; s_first != s_last; ++s_first) {
        for (auto t_it = s_last; t_it != last; ++t_it) {
            if (edge(*s_first, *t_it) && --in_degree[t_it - first] == 0) {
                std::swap(*t_it, *s_last);
                std::swap(in_degree[t_it - first], in_degree[s_last - first]);
                ++s_last;
            }
        }
    }
}

template <std::random_access_iterator I, class S, class F>
bool is_topologically_sorted(I first, S last, F edge) {
    for (; first != last; ++first) {
        for (auto it = std::next(first); it != last; ++it) {
            if (edge(*it, *first)) {
                return false;
            }
        }
    }
    return true;
}

int main() {
    std::string vertices = "235789AB";
    auto edge = [](char u, char v) {
        return (u == '5' && v == 'B') || (u == '7' && v == 'B') ||
               (u == '7' && v == '8') || (u == '3' && v == '8') ||
               (u == '3' && v == 'A') || (u == 'B' && v == '2') ||
               (u == 'B' && v == '9') || (u == 'B' && v == 'A') ||
               (u == '8' && v == '9');
    };

    do {
        auto sorted = vertices;
        // std::sort(sorted.begin(), sorted.end(), edge); // Does not work!
        topological_sort(sorted.begin(), sorted.end(), edge);
        if (!is_topologically_sorted(sorted.begin(), sorted.end(), edge)) {
            std::cout << vertices << " --> " << sorted << '\n';
        }
    } while (std::next_permutation(vertices.begin(), vertices.end()));
}
