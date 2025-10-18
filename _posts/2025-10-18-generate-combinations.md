---
title: "Generate Combinations in C++"
date: 2025-10-18
---


## Intro

The other way, I was checking my old Github repos. One caught my attention - I was trying to implement Python's [`itertools`](https://docs.python.org/3/library/itertools.html) module in C++. Well, that was before even C++20, and apparently there have been a few `cpp-itertools` attempts by others too. Since then, a lot of the `itertools` features eventually *did* end up in the C++ standard library (`<ranges>`), such as `zip`, `enumerate`, `product` (`cartesian_product`), and a few more.

One, however, remains at large - `combinations` - returns `r` length subsequences of elements from the input `iterable`.

The Python version looks like this:

```py
nums = [1, 2, 3]
for comb in itertools.combinations(nums, 2):
    print(comb)
# (1, 2)
# (1, 3)
# (2, 3)
```

I was looking at how I converted it to C++. Well:

```cpp
std::vector<int> nums{1, 2, 3};
for (auto&& [a, b] : itertools::combinations<2>(nums))
{
    std::cout << a << " " << b << std::endl;
}
// 1 2
// 1 3
// 2 3
```

I suppose I took the "fixed length" to a new level - making `r` a compile time constant. This makes the implementation easier - it's just a `tuple` of iterators - at the cost of quite some functionalities. I mean, if the length is known at compile time, we can just use nested loops!

Can I do better now?



## combinations() with runtime r

What if we just make `r` a regular function parameter?

```cpp
template <class R>
/* range-adaptor-type */ combinations(R&& rg, size_t r);
```

What should the value type of `/* range-adaptor-type */` be? It has to be a range itself, because it represents a specific combination (or, a *slice*) of the original range `rg`.

`vector<T>` is straightforward, but otherwise inferior in many ways: allocation in every step, elements may not be copyable, etc.

Some lazy range that finds the next element as it goes? For example, the iterator contains a bit vector of size `N` where exactly `r` bits are set and an iterator `it` of the original range:
- Iterating the outer range updates the bit vector and sets `it` back to `rg.begin()`
- Iterating the inner range scans for the next active bit, and increments `it`

This will not need allocation in every step, but it still needs to allocates and store the bic-vector state, which has space complexity `O(N)`.



### next_combination()

What if we modify the elements in place, just like `std::next_permutation`?

```cpp
template <class R>
bool next_combination(R&& rg, size_t r);
```

Basically, the first `r` elements of `rg` represent the combination. Like `std::next_permutation`, at each call, it finds the next lexicographically greater combination. If it succeeds, it modifies `rg` accordingly and returns `true`; otherwise, the current combination is already the largest, it updates the combination to be the smallest, and returns `false`.

```cpp
std::vector<int> nums{1, 2, 3};
do {
    std::println("{}", nums);
} while (next_combination(nums, 2));
// 1 2 3
// 1 3 2
// 2 3 1
```

One improvement is that, instead of taking `size_t r`, we take an iterator `mid` so that `[first, mid)` represent the combination:

```cpp
template <class R, class Iter>
bool next_combination(R&& rg, Iter mid);
```

This is because `next(rg.begin(), r)` may not be cheap (when `rg` is not random access), and you would need `mid` anyway to actually use the combination.

- Standard library functions also do this: `std::rotate`, `std::nth_element`

So, how do we implement `next_combination`?

```cpp
/// The actual workhouse, where the range-version delegates to
template <class Iter>
bool next_combination(Iter first, Iter mid, Iter last);
```



### Implementing next_combination

We want to find the next lexicographically greater combination.

First, we find the last (rightmost) element in `[first, mid)` that is *incrementable*:

- An element is *incrementable* if there is at least one element in `[mid, right)` that is greater than it
- To increment it is just to swap it with that greater element

Additionally, we want to find the *smallest* element from `[mid, right)` to swap it with. This ensures it is really the *next*.

Take for example that we are generating length-4 combinations of `123456789`, and the current range is:

```
1 4 8 9 | 2 3 5 6 7
  ^           ^
```

The rightmost incrementable element is `4`, and we should swap it with is `5`. After swapping, the range becomes:

```
1 5 8 9 | 2 3 4 6 7
  ^           ^
```

We're not done. All elements after `5` need update as well. The range should eventually be:

```
1 5 6 7 | 2 3 4 8 9
    _ _         _ _
```

It seems that operation is swapping ranges. Well, not really. Consider Example 2:

```
1 5 8 9 | 2 3 4 6 7
  ^             ^

1 6 8 9 | 2 3 4 5 7
    _ _           _

1 6 7 8 | 2 3 4 5 9
    _ _           _
```

And Example 3:

```
1 3 8 9 | 2 4 5 6 7
  ^         ^

1 4 8 9 | 2 3 5 6 7
    _ _       _ _ _

1 4 5 6 | 2 3 7 8 9
    _ _       _ _ _
```

What we really want to is `rotate`, but with a gap in between.
Another way to look at it is that we are rotating a *logical range* that is formed by two disjoint ranges.

- In the above examples, the underlined elements form the logical range.

Hence a helper function, `rotate2`:

```cpp
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
```

With it, `next_combination`:

```cpp
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
```

The full code can be found [here](https://github.com/biowpn/bioweapon/blob/main/codes/next_combination.cpp).



## Prior Work


### next_combination

Unsurprisingly, the idea of `next_combination` came into mind naturally after `next_permutation`. In fact, there is actually a whole paper [N2639: Algorithms for permutations and combinations, with and without repetitions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2639.pdf) by Hervé Brönnimann, which proposes the exact same `next_combination` among a few others.

The implementation of `next_combination` in N2639 is:

```cpp
/// This the actual helper for both `next_combination()` and `prev_combination()`:
///     next_combination(first, mid, last) calls next_combination(first, mid, mid, last)
///     prev_combination(first, mid, last) calls next_combination(mid, last, first, mid)
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
```

The rough steps are similar:

1. Find the rightmost element to increment
2. Increment it
3. Correct the elements followed



### for_each_combination

A different paper, [Combinations and Permutations](https://howardhinnant.github.io/combinations/combinations.html) by Howard Hinnant, proposes that combinations should be iterated in `for_each` style:

```cpp
template <class BidirIter, class Function>
Function
for_each_combination(BidirIter first,
                     BidirIter mid,
                     BidirIter last,
                     Function f);
```

The paper is aware of N2639, and the stated reason for `for_each` style interface is performance:

> The problem with this (next_combination) interface is that it can get expensive to find the next iteration.

and:

> It is simply that the number of comparisons that need to be done to find out which swaps need to be done gets outrageously expensive. The number of swaps actually performed in both algorithms is approximately the same. At N == 100 for_each_combination is running about 14 times faster than next_combination. And that discrepancy only grows as the number of combinations increases.

I have not benchmarked my implementation against either N2639's `next_combination` or `for_each_combination`, but it may be interesting to find out whether my binary search + gapped rotate approach saves some comparisons. (When I have time in the future, probably, but not now; I've already spent an entire Saturday night working on this instead of video games or other less mentally taxing activities. Talk about needing a life.)



## Afterword

I can see why we don't have `std::next_combination`. If Howard is right, then there's a tradeoff to be made between consistent interface (`next_combination`) and performance (`for_each_combination`). Is it better to have something sub-optimal in the standard library or nothing at all? I think different people will have different answers. Anyway, it's fun to implement `next_combination`. And that's the whole point.
