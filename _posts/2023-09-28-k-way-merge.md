---
title: "Implementing K-Way Merge"
date: 2023-09-27
---


## Intro

Merging sorted ranges into one sorted range is a common task.
In the standard library, we have `std::ranges::merge`:

```cpp
// Merges two sorted ranges r1, r2 into one sorted range beginning at result.
template< ranges::input_range R1, ranges::input_range R2,
          class O /*, ... */ >
constexpr /* ... */ merge( R1&& r1, R2&& r2, O result /*, ... */ );
```

But it's for merging exactly 2 ranges.
The general form of the problem is: given *k* sorted ranges, merge them into one sorted range.

This is known as [k-way merge](https://en.wikipedia.org/wiki/K-way_merge_algorithm). Let's implement it.


## API Design

First of all, what should our API look like? There isn't a precedent function in the standard `<algorithm>` library that takes multiple ranges as input. They take at most two (and usually one) ranges.

But even before that, we need to ask: do we know `k` at compile time? If so, the API can look like:

```cpp
template< ranges::input_range ... Rs,
          class O>
constexpr /* ... */ k_merge( Rs&& ... rs, O result);

// usage:
// k_merge(r1, r2, out);
// k_merge(r1, r2, r3, out);
```

Otherwise (`k` only known at runtime), we have to introduce "range of ranges":

```cpp
template< ranges::input_range Rs,
          class O>
constexpr /* ... */ k_merge( Rs&& rs, O result);

// usage:
// k_merge({r1, r2}, out);
// k_merge({r1, r2, r3}, out);
```

It turns out they are two very different problems.
Today we'll only talk about the runtime case as it is more general and easier to deal with - all input ranges are of the same type.

To keep it simple, we'll simply return `result` (like `std::ranges`), and leaving the custom comparison function out:

```cpp
template< ranges::input_range Rs,
          class O>
constexpr O k_merge( Rs&& rs, O result);
```


## Normalize

The first problem is, the `input_range` is too limited.
Remember that `input_range` means it's only good for one-pass algorithms; but even the "scan the minimum" algorithm is multi-pass: it needs to visit each element (input sorted range) multiple times. The only single-pass way is brute-force: copy all elements into a big array, sort that array, and output. Clearly that's not efficient.

We can change our API from `input_range` to, for example, `forward_range`. But that would make it less functional, and even then, `forward_range` is not enough for the more efficient heap-based algorithm which requires `random_access_range`. And even we go all in, the input ranges may be non-modifiable, so we can't really `pop_front()` each input range of `rs` in-place.

The answer is: copy the iterators into a more powerful container, say `std::vector`, and operate on that instead. For each input range, we turn it into pair of `begin()` and `end()`. So we need a type holds two possibly-different-type values.

We could use `std::pair`, but there is a better option: `std::ranges::subrange`. It comes with handy member functions including `advance()` that essentially drops the first element(s).

Therefore, the first part of our function is to *normalize* the inputs:

```cpp
template< std::ranges::input_range Rs,
          class O>
constexpr O k_merge( Rs&& rs, O result) {
    using range_t = ranges::range_value_t<Rs>; // input sorted range
    using subrange_t = ranges::subrange<       // our cheap-to-copy view of the input range
        ranges::iterator_t<range_t>,
        ranges::sentinel_t<range_t>
    >;
    std::vector<subrange_t> sub_rs;
    for (auto&& r: rs) {
        subrange_t sub_r{r};
        if (!sub_r.empty()) {
            sub_rs.push_back(std::move(sub_r));
        }
    }
    // ...
}
```

Here we only keep the non-empty ranges. We'll see why.


## Linear Scan

Let's implement the straightforward approach - scan the minimum of `k` ranges each iteration.

We first need a comparison function to compare two input ranges.

```cpp
auto cmp_rg = [](const subrange_t& lhs, const subrange_t& rhs) {
    return *lhs.begin() < *rhs.begin();
};
```

Had we not dropped the non-empty ranges, we would have to check for emptiness here.

Next, the main loop:

```cpp
while (!sub_rs.empty()) {
    auto it_min = ranges::min_element(sub_rs, cmp_rg);
    auto& min_rg = *it_min;
    *result++ = *min_rg.begin();
    min_rg.advance(1);
    if (min_rg.empty()) {
        // maintain the invariance that `sub_rs` holds non-empty ranges
        sub_rs.erase(it_min);
    }
}
```

The time complexity is `O(n * k)` where `n` is the total number of elements.


## Heap

When `k` is large, it is expensive to do `min_element` on each loop. Instead we can maintain a heap of the minimums:

```cpp
auto cmp_rg = [](const subrange_t& lhs, const subrange_t& rhs) {
    // std::make_heap produces a max heap by default, so we need to revert the side here.
    return *rhs.begin() < *lhs.begin();
};

std::ranges::make_heap(rs, cmp_rg);
while (!sub_rs.empty()) {
    std::ranges::pop_heap(rs, cmp_rg);
    auto& min_rg = rs.back();
    *result++ = *min_rg.begin();
    min_rg.advance(1);
    if (min_rg.empty()) {
        rs.pop_back();
    } else {
        std::ranges::push_heap(rs, cmp_rg);
    }
}
```

This reduces the time complexity to `O(n * log(k))`.


## Make it Stable

Note that there is one minor problem of the above heap-based algorithm - it isn't stable.
Here, being stable means that if two elements from two different input ranges compare equal,
the one whose input range appears first in `rs` will appear first in the output.

The fix is to introduce a unique index for each input range to serve as the tie breaker:

```cpp
struct index_subrange_t : public subrange_t {
    std::size_t index{}; // tie-breaker
    // ... add the necessary constructors ...
};

std::vector<index_subrange_t> sub_rs;
std::size_t i = 0;
for (auto&& r: rs) {
    index_subrange_t sub_r{r};
    if (!sub_r.empty()) {
        sub_r.index = i++;
        sub_rs.push_back(std::move(sub_r));
    }
}
```

and modify the comparison function:

```cpp
auto cmp_rg = [](const index_subrange_t& lhs, const index_subrange_t& rhs) {
    const auto& top1 = *lhs.begin();
    const auto& top2 = *rhs.begin();
    if (top2 < top1) return true;
    else if (top1 < top2) return false;
    else return rhs.index < lhs.index;
};
```


## Algorithm selection

While heap-based algorithms give the best time complexity, they aren't necessarily the fastest when `k` isn't big.
This is because moving items around the heap, while cheap, isn't free.

What we can do it to choose the algorithm based on `k`: only employ the heap algorithm when `k` reaches some predefined threshold.

In fact, while we are at it:
- If `k == 0`, call it a day!
- Else If `k == 1`, simply `std::ranges::copy` to output
- Else If `k == 2`, use `std::ranges::merge`
- Else If `k < THRESHOLD`, use linear scanning
- Else, use heap

In addition, we won't just select algorithm one-off at the start; we keep re-evaluating whether we should switch algo each time `k` changes - decreases by 1 when an input range becomes empty and is removed from `sub_rs`.

In fact, if you check the actual implementations ([libstdc++](https://github.com/gcc-mirror/gcc/blob/d9375e490072d1aae73a93949aa158fcd2a27018/libstdc%2B%2B-v3/include/bits/stl_algo.h#L4856), [libc++](https://github.com/llvm-mirror/libcxx/blob/a12cb9d211019d99b5875b6d8034617cbc24c2cc/include/algorithm#L4348)) of `std::merge`, you'll see that they fallback to `std::copy` whenever an input range becomes empty. Essentially the same idea.
