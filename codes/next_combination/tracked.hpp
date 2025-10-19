#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <utility>

namespace tracked_stats {

static inline std::size_t value_comparisons = 0;
static inline std::size_t value_swaps = 0;
static inline std::size_t iter_comparisons = 0;
static inline std::size_t iter_increments = 0;

} // namespace tracked_stats

template <typename T>
struct tracked {
    T value;

    auto operator<=>(const tracked& other) const {
        ++tracked_stats::value_comparisons;
        return value <=> other.value;
    }

    bool operator==(const tracked& other) const {
        ++tracked_stats::value_comparisons;
        return value == other.value;
    }

    friend void swap(tracked& a,
                     tracked& b) noexcept(std::is_nothrow_swappable_v<T>) {
        ++tracked_stats::value_swaps;
        using std::swap;
        swap(a.value, b.value);
    }
};

template <typename Iter>
struct tracked_iterator {
    using iterator_category =
        typename std::iterator_traits<Iter>::iterator_category;
    using value_type = typename std::iterator_traits<Iter>::value_type;
    using difference_type =
        typename std::iterator_traits<Iter>::difference_type;
    using pointer = typename std::iterator_traits<Iter>::pointer;
    using reference = typename std::iterator_traits<Iter>::reference;

    Iter iter;

    tracked_iterator() = default;
    explicit tracked_iterator(Iter i) : iter(i) {
    }

    // === Forward Iterator Support ===

    reference operator*() const {
        return *iter;
    }
    pointer operator->() const {
        return iter.operator->();
    }

    tracked_iterator& operator++() {
        ++tracked_stats::iter_increments;
        ++iter;
        return *this;
    }

    tracked_iterator operator++(int) {
        tracked_iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    // === Bidirectional Support ===

    tracked_iterator& operator--() {
        ++tracked_stats::iter_increments;
        --iter;
        return *this;
    }

    tracked_iterator operator--(int) {
        tracked_iterator tmp = *this;
        --(*this);
        return tmp;
    }

    bool operator==(const tracked_iterator& other) const {
        ++tracked_stats::iter_comparisons;
        return iter == other.iter;
    }

    // === Random Access Support (conditionally) ===

    tracked_iterator& operator+=(difference_type n) {
        ++tracked_stats::iter_increments;
        iter += n;
        return *this;
    }

    tracked_iterator& operator-=(difference_type n) {
        ++tracked_stats::iter_increments;
        iter -= n;
        return *this;
    }

    tracked_iterator operator+(difference_type n) const {
        tracked_iterator tmp = *this;
        return tmp += n;
    }

    friend tracked_iterator operator+(difference_type n,
                                      const tracked_iterator& it) {
        return it + n;
    }

    tracked_iterator operator-(difference_type n) const {
        tracked_iterator tmp = *this;
        return tmp -= n;
    }

    difference_type operator-(const tracked_iterator& other) const {
        ++tracked_stats::iter_comparisons;
        return iter - other.iter;
    }

    reference operator[](difference_type n) const {
        return iter[n];
    }

    auto operator<=>(const tracked_iterator& other) const {
        ++tracked_stats::iter_comparisons;
        return iter <=> other.iter;
    }
};
