
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

template <class BidirectionalIterator>
bool next_combination(BidirectionalIterator first1, BidirectionalIterator last1,
                      BidirectionalIterator first2,
                      BidirectionalIterator last2) {
    if ((first1 == last1) || (first2 == last2)) {
        return false;
    }
    BidirectionalIterator m1 = last1;
    BidirectionalIterator m2 = last2;
    --m2;
    while (--m1 != first1 && !(*m1 < *m2)) {
    }
    bool result = (m1 == first1) && !(*first1 < *m2);
    if (!result) {
        while (first2 != m2 && !(*m1 < *first2)) {
            ++first2;
        }
        first1 = m1;
        std ::iter_swap(first1, first2);
        ++first1;
        ++first2;
    }
    if ((first1 != last1) && (first2 != last2)) {
        m1 = last1;
        m2 = first2;
        while ((m1 != first1) && (m2 != last2)) {
            std ::iter_swap(--m1, m2);
            ++m2;
        }
        std ::reverse(first1, m1);
        std ::reverse(first1, last1);
        std ::reverse(m2, last2);
        std ::reverse(first2, last2);
    }
    return !result;
}

template <class Iter>
void rotate2(Iter first1, Iter last1, Iter first2, Iter last2) {
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

    // Find the last element in [first, mid) that is
    // smaller than some element in [mid, last)
    auto left = std::lower_bound(first, mid, *std::prev(last));
    // left is the first element >= max, so (left - 1) must be < max
    if (left == first) {
        std::rotate(first, mid, last);
        return false;
    }
    --left;
    auto right = std::upper_bound(mid, last, *left);

    // Swap them
    std::iter_swap(left, right);

    // Correct [left + 1, mid) by doing a gapped rotate
    rotate2(std::next(left), mid, std::next(right), last);

    return true;
}

void test_and_show(std::string str, size_t r) {
    std::cout << "combinations(" << str << ", " << r << "):\n";
    do {
        std::cout << "  " << str << '\n';
    } while (next_combination(str.begin(), str.begin() + r, str.end()));
    std::cout << "\n";
}

int main() {
    test_and_show("ABCDE", 0);
    test_and_show("ABCDE", 1);
    test_and_show("ABCDE", 2);
    test_and_show("ABCDE", 3);
    test_and_show("ABCDE", 4);
    test_and_show("ABCDE", 5);
}
