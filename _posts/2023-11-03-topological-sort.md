---
title: "Generic Topological Sort"
date: 2023-11-03
---

## Intro

[Topological sorting](https://en.wikipedia.org/wiki/Topological_sorting) comes up often in graph-based applications.
For example, task schedulers, dependency resolvers, and node-based computation engines. Let's try to design and implement generic topological sorting in C++.


## Can we reuse `std::sort`?

As a rule of thumb, if there's something in the standard library that solves our problem,
we should preferably use them.

We have a candidate: `std::sort` (and `std::ranges::sort`). Its API looks like:

```cpp
template< ranges::random_access_range R, class Comp = ranges::less, /* ... */ >
/* ... */ sort( R&& r, Comp comp = {}, /* ... */ );
```

From the looks of it, we can supply our list of vertices as `r`, and hack around `comp` so that it provides information on the edges of the graph. In fact, intuitively, we can define it as:

- `comp(a, b)` returns true iff there is an edge from `a` to `b`

This naturally models the "`a` comes before `b`" relationship as "`a` compares less-than `b`".

Better yet, paraphrasing [the sort documentation](https://en.cppreference.com/w/cpp/algorithm/sort), the postcondition of `std::sort` is:

> `comp(j, i) == false` for **every** pair of element `i` and `j` where `i` is before `j` in the sequence.

In other words, if there is an edge from `i` to `j` (`comp(i, j) == true`), then `i` must be before `j` in the sequence.

And if we compare it to the definition of [topological ordering](https://en.wikipedia.org/wiki/Topological_sorting):

> ... is a linear ordering ... such that for every directed edge *uv* from vertex *u* to vertex *v*, *u* comes before *v* in the ordering.

It's a perfect match!

So, can we simply

```cpp
/// `edge(u, v)` is true if there is an edge from `u` to `v`
template< ranges::random_access_range R, class F >
void topological_sort( R&& vertices, F edge ) {
    std::ranges::sort(vertices, edge);
}
```

and call it a day? Does it work?


## It Does Not.

Let's try it with an example. The graph looks like this:

![plot](../images/tp_sort_graph_1.png)

There are exactly 3 topological orderings of the graph:
- `ABCD`
- `ACBD`
- `ACDB`

The following program generates all permutations of the vertices, supplies them to `std::ranges::sort`, and shows the output:

```cpp

#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

auto& operator<<(std::ostream& os, const std::vector<char>& x) {
    return os << std::string_view(x.data(), x.size());
}

int main() {
    std::vector<char> vertices{'A', 'B', 'C', 'D'};
    auto edge = [](char u, char v) {
        return (u == 'A' && v == 'B')
            || (u == 'A' && v == 'C')
            || (u == 'C' && v == 'D');
    };

    do {
        auto cur = vertices;
        std::ranges::sort(cur, edge);
        std::cout << vertices << " --> " << cur << '\n';
    } while (std::ranges::next_permutation(vertices).found);
}
```

Compiled using GCC 13.1.0 and executed, the above program prints:

```
ABCD --> ABCD
ABDC --> ABCD
ACBD --> ACBD
ACDB --> ACDB
ADBC --> ADBC
ADCB --> ACDB
BACD --> ABCD
BADC --> ABCD
BCAD --> ABCD
BCDA --> ABCD
BDAC --> ABCD
BDCA --> ABCD
CABD --> ACBD
CADB --> ACDB
CBAD --> ACBD
CBDA --> ACBD
CDAB --> ACDB
CDBA --> ACDB
DABC --> CDAB
DACB --> CDAB
DBAC --> CDAB
DBCA --> ACDB
DCAB --> ACDB
DCBA --> ACDB
```

As we can see, there are 3 cases where the outputs are wrong:

```
DABC --> CDAB
DACB --> CDAB
DBAC --> CDAB
```

So what's going here?


## Preconditions of `std::sort`

As a rule of thumb x2, if a standard library algorithm, especially a long-lived and well-tested one like `std::sort`, produces unexpected results, it's most likely our fault of providing bad inputs - inputs that violate the preconditions.

But in our cases:
- We provided a random-access sequence
- We provided a comparator that matches the required signature

And in fact, in any of the above fails to hold, we would get a hard compile error.

Reading again the [the sort documentation](https://en.cppreference.com/w/cpp/algorithm/sort), more carefully this time:

> **comp** - comparison function object (i.e. an object that satisfies the requirements of *Compare*) which returns `​true` if the first argument is less than (i.e. is ordered *before*) the second.

In the [documentation of Compare](https://en.cppreference.com/w/cpp/named_req/Compare), we have this section:

> Establishes *strict weak ordering* relation with the following properties:
1. For all `a`, `comp(a, a) == false`.
2. If `comp(a, b) == true` then `comp(b, a) == false`.
3. If `comp(a, b) == true` and `comp(b, c) == true` then `comp(a, c) == true`.

`edge` satisfies 1 and 2, but not 3: `edge(A, C) == true` and `edge(C, D) == true`, but `edge(A, D) == false`.

So that must be it, right? We just need to change `edge` to `is_path` and it'll work?

Hold on a second. There's another section on `equiv(a, b)`, which is defined as `!comp(a, b) && !comp(b, a)` (`a` and `b` are considered *equal* if neither precedes the other), as follows:

> Establishes *equivalence relationship* with the following properties:
...
4. If `equiv(a, b) == true` and `equiv(b, c) == true`, then `equiv(a, c) == true`

4 is where the real trouble is.

In our example, `equiv(B, C) == true` and `equiv(B, D) == true`, therefore `std::sort` goes ahead and assumes `equiv(C, D) == true`, which is incorrect. Even if we replace `edge` with `is_path`, 4 is still violated.

But why is this such a big deal?

Let's forget `std::sort` for now and take a look at the simpler `std::is_sorted`. Recall the definition of "sorted sequence" states that *every* pair satisfies some condition, so there should be `O(N^2)` checks to be thorough, but `std::is_sorted` runs in `O(N)` time. How is that possible?

As you may already know it, `std::is_sorted` only checks the adjacent pairs. All the rest are inferred, and it is allowed to do so because of the properties of *Compare*! As an example, `std::is_sorted` will happily consider `DABC` "sorted", while in fact it is not.

In general, any algorithm that requires *Compare* (read: *strict weak ordering*), including `std::sort`, cannot be used to implement topological sorting because the vertices of a directed acyclic graph (DAG) do not form a strict weak ordering.


## What do we do instead?

We can stick to `std::sort`-like API:

```cpp
/// `edge(u, v)` is true if and only if there is an edge from `u` to `v`
template <std::random_access_iterator I, class S, class F>
void topological_sort(I first, S last, F edge);
```

The naive way is to do a modified insert sort:

```cpp
/// topological sort, the brute force
template <std::random_access_iterator I, class S, class F>
void topological_sort_v1(I first, S last, F edge) {
    for (; first != last; ++first) {
        // check if *first is a source.
        for (auto other = std::next(first); other != last; ++other) {
            if (edge(*other, *first)) {
                // *first is not a source; *other may be
                std::swap(*other, *first);
                // have to do the full search again for the new *first
                other = first;
            }
        }
    }
}
```

This is simple, requires constant extra memory, but what about the time complexity?

Note that due to the reset `other = first` inside the nested loop,
this brute force solution could be `O(|V|^3)`, where `|V|` is the number of vertices.

Alternatively, we could implement Kahn's algorithm. The rough idea is:

1. Put all the sources into a set `S`
2. Remove a source, `u`, from `S`, and output `u`
3. Remove all outgoing edges of `u`
4. Some (previous) direct successor of `u`, `v`, may become a source; if so, add `v` to `S`
5. Go back to step 2 until `S` is empty

```cpp
/// topological sort, Kahn's algorithm
template <std::random_access_iterator I, class S, class F>
void topological_sort_v2(I first, S last, F edge) {
    using bit_vector = std::vector<bool>;

    std::size_t n = std::ranges::distance(first, last);
    bit_vector m(n * n); // adjacency matrix
    std::vector<std::size_t> in_degree(n);
    std::queue<std::size_t> sources;

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            m[n * i + j] = edge(first[i], first[j]);
            m[n * j + i] = !m[n * i + j] && edge(first[j], first[i]);
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            in_degree[i] += bool(m[n * j + i]);
        }
        if (in_degree[i] == 0) {
            sources.push(i);
        }
    }

    std::vector<std::size_t> order; // tp-sorted, as indices
    order.reserve(n);

    while (!sources.empty()) {
        auto i = sources.front();
        sources.pop();
        order.push_back(i);
        for (std::size_t j = 0; j < n; ++j) {
            if (m[n * i + j]) {
                m[n * i + j] = false;
                if (--in_degree[j] == 0) {
                    sources.push(j);
                }
            }
        }
    }

    reorder(first, last, order.begin());
}
```

where `reorder` reorders a range based on another range of indices:

```cpp
/// Reorder [first, last) based on indices from [order, order + (last - first))
/// Based on https://stackoverflow.com/a/22183350
/// Linear time.
template <std::random_access_iterator I, class S, std::random_access_iterator O>
void reorder(I first, S last, O order)
{
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
```

Kahn's algorithm runs in `O(|V| + |E|)`, `|E|` is the number of edges.
For a dense graph, `|E| ~ |V|^2`, therefore our algorithm runs in `O(|V|^2)`.

Instead of using `reorder` to sort the input range in-place,
we can take an output iterator to collect the sorted sequence:

```cpp
template <std::random_access_iterator I, class S, class F, class O>
void topological_sort_copy(I first, S last, F edge, O result);
```


## Afterword

We showed that why `std::[ranges::]sort` cannot be used for topological sorting,
and then implemented topological sorting under a similar API.

Perhaps, a few more finite graph algorithms can be implemented based on the API:

```cpp
template <std::random_access_iterator I, class S, class F>
/* ... */ f(I first, S last, F edge);
```

Am I smelling `std::graphs::sort`? Maybe one day. Maybe.
