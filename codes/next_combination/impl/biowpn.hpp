#pragma once

#include <algorithm>

namespace biowpn {

template <class Iter>
void rotate_disjoint(Iter first1, Iter last1, Iter first2, Iter last2) {
    const auto n1 = std::distance(first1, last1);
    const auto n2 = std::distance(first2, last2);
    if (n1 <= n2) {
        auto mid2 = std::swap_ranges(first1, last1, first2);
        std::rotate(first2, mid2, last2);
    } else {
        auto mid1 = std::prev(last1, n2);
        std::swap_ranges(mid1, last1, first2);
        std::rotate(first1, mid1, last1);
    }
}

template <class Iter>
bool next_combination(Iter first, Iter mid, Iter last) {
    if (first == mid || mid == last) {
        return false;
    }
    auto left = std::lower_bound(first, mid, *std::prev(last));
    if (left == first) {
        std::rotate(first, mid, last);
        return false;
    }
    --left;
    auto right = std::upper_bound(mid, last, *left);
    std::iter_swap(left, right);
    rotate_disjoint(std::next(left), mid, std::next(right), last);
    return true;
}

} // namespace biowpn
