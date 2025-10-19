---
title: "Generate Combinations in C++"
date: 2025-10-18
---

## Intro

The other day, I was checking my old GitHub repos. One caught my attention - I was trying to implement Python's [`itertools`](https://docs.python.org/3/library/itertools.html) module in C++. Well, that was before even C++20, and apparently there have been a few `cpp-itertools` attempts by others too. Since then, a lot of the `itertools` features eventually *did* end up in the C++ standard library (`<ranges>`), such as `zip`, `enumerate`, `product` (`cartesian_product`), and a few more.

One, however, remains elusive: `combinations`, which returns `r`-length subsequences of elements from the input `iterable`.

The Python version looks like this:

```py
nums = [1, 2, 3]
for comb in itertools.combinations(nums, 2):
    print(comb)
# (1, 2)
# (1, 3)
# (2, 3)
```

I was looking at how I had converted it to C++. Well:

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

I suppose I took the "fixed length" idea to a new level - making `r` a compile-time constant. This makes the implementation easier - it's just a `tuple` of iterators - but comes at the cost of flexibility. I mean, if the length is known at compile time, we can just use nested loops!

So, can I do better now?

## combinations() with runtime `r`

What if we just make `r` a regular function parameter?

```cpp
template <class R>
/* range-adaptor-type */ combinations(R&& rg, size_t r);
```

What should the value type of `/* range-adaptor-type */` be? It has to be a range itself, because it represents a specific combination (or, a *slice*) of the original range `rg`.

`vector<T>` is straightforward, but inferior in many ways: allocation at every step, elements may not be copyable, etc.

What about a lazy range that finds the next element as it goes? For example, the iterator contains a bit vector of size `N`, where exactly `r` bits are set, and an iterator `it` over the original range:
- Iterating the outer range updates the bit vector and resets `it` to `rg.begin()`
- Iterating the inner range scans for the next active bit and increments `it`

This avoids allocating on each step, but it still needs to allocate and store the bit-vector state, which has space complexity `O(N)`.

### next_combination()

What if we modify the elements in place, just like `std::next_permutation`?

```cpp
template <class R>
bool next_combination(R&& rg, size_t r);
```

Basically, the first `r` elements of `rg` represent the combination. Like `std::next_permutation`, each call finds the next lexicographically greater combination. If it succeeds, it modifies `rg` accordingly and returns `true`; otherwise, the current combination is already the largest, and it resets to the smallest combination and returns `false`.

```cpp
std::vector<int> nums{1, 2, 3};
do {
    std::println("{}", nums);
} while (next_combination(nums, 2));
// 1 2 3
// 1 3 2
// 2 3 1
```

One improvement is to use an iterator `mid` instead of taking `size_t r`, so that `[first, mid)` represents the combination:

```cpp
template <class R, class Iter>
bool next_combination(R&& rg, Iter mid);
```

This is because `std::next(rg.begin(), r)` may not be cheap (when `rg` is not random-access), and you'd need `mid` anyway to actually use the combination.

- Standard library functions do this too: `std::rotate`, `std::nth_element`

So, how do we implement `next_combination`?

```cpp
/// The actual workhorse, which the range version delegates to
template <class Iter>
bool next_combination(Iter first, Iter mid, Iter last);
```

### Implementing next_combination

We want to find the next lexicographically greater combination.

First, we find the last (rightmost) element in `[first, mid)` that is *incrementable*:

- An element is *incrementable* if there is at least one element in `[mid, last)` that is greater than it
- To increment it is just to swap it with such a greater element

Additionally, we want to find the *smallest* such element from `[mid, last)` to swap it with. This ensures that the result is truly the *next* combination.

Take this example: generating length-4 combinations of `123456789`, and the current sequence is:

```
1 4 8 9 | 2 3 5 6 7
  ^           ^
```

The rightmost incrementable element is `4`, and we should swap it with **5**. After swapping:

```
1 5 8 9 | 2 3 4 6 7
  ^           ^
```

We're not done. All elements after `5` need updating too. The range should eventually become:

```
1 5 6 7 | 2 3 4 8 9
    _ _         _ _
```

It seems like the operation is range swapping. But not quite. Consider this variation:

```
1 5 8 9 | 2 3 4 6 7
  ^             ^

1 6 8 9 | 2 3 4 5 7
    _ _           _

1 6 7 8 | 2 3 4 5 9
    _ _           _
```

Or this one:

```
1 3 8 9 | 2 4 5 6 7
  ^         ^

1 4 8 9 | 2 3 5 6 7
    _ _       _ _ _

1 4 5 6 | 2 3 7 8 9
    _ _       _ _ _
```

What we really want is a `rotate` - but with a *gap* in between. You can also think of it as rotating a *logical range* composed of two disjoint ranges.

So we define a helper function `rotate_disjoint`:

```cpp
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
```

Now `next_combination` becomes:

```cpp
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
```

The full code can be found [here](https://github.com/biowpn/bioweapon/blob/main/codes/next_combination).

## Prior Work

### next_combination

Unsurprisingly, the idea of `next_combination` arose naturally after `next_permutation`. In fact, there's an entire paper: [N2639: Algorithms for permutations and combinations, with and without repetitions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2639.pdf) by Hervé Brönnimann, which proposes the same `next_combination` among others.

The rough steps are similar:
1. Find the rightmost element to increment
2. Increment it
3. Fix the following elements

Whereas I use binary search to find both the rightmost incrementable element and its swap target, Hervé’s implementation performs backward linear scans for both. We also differ in the reordering step.

I've extracted the relevant bits from the paper and pasted the code [here](https://github.com/biowpn/bioweapon/blob/main/codes/next_combination/impl/bronnimann.hpp).

### for_each_combination

Another paper, [Combinations and Permutations](https://howardhinnant.github.io/combinations/combinations.html) by Howard Hinnant, suggests using a `for_each`-style interface.

```cpp
template <class BidirIter, class Function>
Function
for_each_combination(BidirIter first,
                     BidirIter mid,
                     BidirIter last,
                     Function f);
```

The paper is aware of N2639, and gives performance as the reason for this choice:

> The problem with this (next_combination) interface is that it can get expensive to find the next iteration.

> It is simply that the number of comparisons that need to be done to find out which swaps need to be done gets outrageously expensive. The number of swaps actually performed in both algorithms is approximately the same. At N == 100 for_each_combination is running about 14 times faster than next_combination. And that discrepancy only grows as the number of combinations increases.

On an interesting note, I found `rotate_discontinuous` in Howard's implementation, which does exactly the same thing as my `rotate_disjoint`!

I've copied and pasted the code [here](https://github.com/biowpn/bioweapon/blob/main/codes/next_combination/impl/hinnant.hpp).

I haven not benchmarked my version against N2639's `next_combination` or `for_each_combination`, but it may be interesting to find out whether my **binary search + gapped rotate** approach saves some comparisons.

*When I have time in the future - probably. I already spent an entire Saturday night working on this instead of video games or other less mentally taxing activities. Talk about needing a life.*

## Afterword

I can see why we don't have `std::next_combination`. If Howard is right, then there's a tradeoff between consistent interfaces (`next_combination`) and performance (`for_each_combination`). Is it better to have something suboptimal in the standard library, or nothing at all? Different people will have different answers.

Anyway, it was fun to implement `next_combination`. And that's the whole point.
