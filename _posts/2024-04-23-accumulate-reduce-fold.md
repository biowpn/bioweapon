---
title: "accumulate, reduce, and fold"
date: 2024-04-23
---


## Intro

Remember [all the flavors of standard function wrappers](https://biowpn.github.io/bioweapon/2024/01/18/what-the-func.html)? As it turns out, it is far from the only case in standard C++ where we have multiple ways to solve the same problem subtly differently.

Let's begin with a simple task: how do you sum up a range of numbers?



## Raw For Loop

```cpp
double sum(std::span<double> nums) {
    double s = 0;
    for (auto x : nums) {
        s += x;
    }
    return s;
}
```

There really isn't much to say about the above code,
except perhaps the million ways to initialize `s`.

What does the compiler generate? GCC 13.2 on x86-64 with `-O2 -DNDEBUG -std=c++23` [produces](https://godbolt.org/z/aofq4Krf1):

```s
sum(std::span<double, 18446744073709551615ul>):
        lea     rax, [rdi+rsi*8]
        pxor    xmm0, xmm0
        cmp     rdi, rax
        je      .L4
        mov     rdx, rax
        sub     rdx, rdi
        and     edx, 8
        je      .L3
        addsd   xmm0, QWORD PTR [rdi]
        add     rdi, 8
        cmp     rdi, rax
        je      .L12
.L3:
        addsd   xmm0, QWORD PTR [rdi]
        add     rdi, 16
        addsd   xmm0, QWORD PTR [rdi-8]
        cmp     rdi, rax
        jne     .L3
        ret
.L4:
        ret
.L12:
        ret
```

The interesting thing here is:
The compiler checks whether there are a even number of numbers.
If so, it adds two numbers in one loop iteration;
otherwise, it adds one number first, then proceeds as the even case.
Pretty slick!

In either case, and importantly, it adds the numbers in the exact same order as the input.



## std::accumulate

We have the classic `accumulate`. Let's try it:

```cpp
double sum_accumulate(std::span<double> nums) {
    return std::accumulate(nums.begin(), nums.end(), 0.0);
}
```

The compiler generates the exact same assembly as the raw for-loop, which is what we want for zero-overhead abstraction. Great.

`accumulate` has some pitfalls, though. For one, what happens had I written

```cpp
    std::accumulate(nums.begin(), nums.end(), 0);
```

This compiles and runs, but likely gives wrong results. Recall that `accumulate` is


```cpp
template< class InputIt, class T >
T accumulate( InputIt first, InputIt last, T init );
```

The return type `T` depends solely on the third argument `init`.
We give `0`, so `T` is `int`, but we're summing up floats ... truncation happens! And not just the final result, but at every step of summation, since `T` is also the *result-type*.

The takeaway is that, the 3rd parameter `T init` serves *two* purposes:
1. Provide the initial value, which is the value you get if the range is empty;
2. Provide the result type (which is also the return type).

The second pitfall of `accumulate` is that it lives in `<numeric>`, not `<algorithm>`.
Have you ever got the error `'accumulate' is not a memeber of 'std'`?

Anyhow, despite its gotchas, `accumulate` has been super useful and popular,
largely thanks to its second and more generalized form:

```cpp
template< class InputIt, class T, class BinaryOp >
T accumulate( InputIt first, InputIt last, T init, BinaryOp op );
```

We can give it arbitrary binary operation `op`.
It can be summation, string concatenation, anything goes.
The first pitfall is actually an important feature:
The result-type `T` need not be the same as the element type:

```cpp
std::span<double> nums = ...; // Given this is not empty

// An inefficient way of generating a comma-separated list of numbers.
std::string str = accumulate(nums.begin() + 1, nums.end(), std::to_string(nums[0]),
    [](std::string s, double num){
        return s + "," + std::to_string(num);
    }
);

```

The second form of `accumulate` is very versatile. The class of problem it models is:
- Given:
    - an initial state
    - a series of events
    - logic to update the state by an event
- Compute the final state.

This is a very broad class of problems that goes way beyond summing up numbers.
Gotta love `accumulate`!



## std::reduce

Then came C++17 and we received `std::reduce`.
What are we reducing? *Dimension*.
Like `std::accumulate`, `std::reduce` turns a one-dimensional range into a single element.

So what's the difference between these two?

For one, the looks - I mean the API. `std::reduce` has [six forms](https://en.cppreference.com/w/cpp/algorithm/reduce), the simplest of which looks like:

```cpp
template< class InputIt >
typename std::iterator_traits<InputIt>::value_type
    reduce( InputIt first, InputIt last );
```

Notice that this form of `reduce` doesn't need the `T init` parameter.
The result type is deduced from the provided iterators,
and the initial value is just `{}` of that type.
I suppose aside from dimension, we're reducing arity as well, which is not bad.

But ergonomics is not the only thing that makes `reduce` stand out.
`reduce` is allowed to do the (generalized) summation in any order.

Given `init` and `[a1, a2, a3]`:

```
accumulate:
    ((init + a1) + a2) + a3

reduce:
    ((init + a1) + a2) + a3   OR
    (init + a1) + (a2 + a3)   OR
    ((init + a3) + a2) + a1   OR
    a1 + (a2 + (a3 + init))   OR
    ...
```

This makes `reduce` parallelizable - you can chop the input range into `N` parts and have `N` workers doing one each, then sum up the sub-sums.
The standard library provides this functionality out-of-the-box, by giving the `ExecutionPolicy` overloads to `reduce`.

Of course, this freedom of rearrangement comes at a cost, at the API level:
the binary operation `op` of `reduce` needs to accept all combinations of `result-type` and `element-type` should they differ:
- `op(result-type, element-type)`
- `op(element-type, result-type)`
- `op(result-type, result-type)`
- `op(element-type, result-type)`

In contrast, `accumulate` needs only
- `op(result-type, element-type)`

But even not using the `ExecutionPolicy` overloads, this code

```cpp
double sum_reduce(std::span<double> nums) {
    return std::reduce(nums.begin(), nums.end());
}
```

compiles to [very different assembly](https://godbolt.org/z/bK19xEjvh) from that using `accumulate`.

The interesting part is:

```s
.L26:
        movsd   xmm0, QWORD PTR [rdi]
        movsd   xmm2, QWORD PTR [rdi+16]
        mov     rax, rdx
        add     rdi, 32
        addsd   xmm0, QWORD PTR [rdi-24]
        addsd   xmm2, QWORD PTR [rdi-8]
        sub     rax, rdi
        addsd   xmm0, xmm2
        addsd   xmm1, xmm0
        cmp     rax, 24
        jg      .L26
```

Here *four* numbers are added in one loop iteration, just not in the input order.
Suppose the input is `[a0, a1, a2, a3]`, the above is equivalent to:

```
s += (a0 + a1) + (a2 + a3)
```

`accumulate` isn't allowed to do this, because it is required to sum in the input order.
It must do:

```
s += a0
s += a1
s += a2
s += a3
```

You may ask - isn't the result the same? For IEEE 754 floating point, it is not:

- Suppose `[a0, a1, a2] = [1.0, FLT_MAX, -FLT_MAX]`
- `(a0 + a1) + a2` gives you `+inf`
- `a0 + (a1 + a2)` gives you `1`

In other words, floating point addition is not associative.

In general, `reduce` would like `op` to be commutative (`op(a, b) == op(b, a)`) and associative (`op(op(a, b), c) == op(a, op(b, c))`). If not, the behavior is *non-deterministic*.
Now, how many times do you see the standard uses the word "non-deterministic"?
I'll take it anytime over "undefined behavior"!

Oh, did I mention that `reduce` lives in `<numeric>` as well?



## std::ranges::fold_left

C++20 gave us ranges. And it didn't take long before people realized that `std::ranges::accumulate` is missing.
This is not surprising at all given how successful `accumulate` is.
See [this SO post](https://stackoverflow.com/questions/63933163/why-didnt-accumulate-make-it-into-ranges-for-c20).

The official answer is: the ranges proposal authors didn't have enough time to add everything to `<ranges>`.
There was a follow-up paper in 2019, [P1813](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1813r0.pdf),
that aimed to add `accumulate` as well as other `<numeric>` algorithms (`reduce` included) to C++23.

Which ... didn't happen. There is no `accumulate` nor `reduce` in `std::ranges` in C++23.
We got `fold_left` instead:

```cpp
template< ranges::input_range R, class T, /* binary-op */ F >
constexpr auto fold_left( R&& r, T init, F f );
```

Like `accumulate`, it needs an `init` value,
and is required to do summation in the input order
(which means it cannot be parallelized).

Unlike `accumulate`, the result type is not `T`. Nor is it the element type like `reduce`.
Rather, it is the return-type of the binary operation `F`.
This theoretically makes it less susceptible to the type-mismatch error:

```cpp
std::span<double> nums;

std::accumulate(nums.begin(), nums.end(), 0);            // Compiles, runs, but not what you want

std::reduce(nums.begin(), nums.end());                   // Ok, result type is double

std::reduce(nums.begin(), nums.end(), 0);                // Compiles, runs, but not what you want
std::reduce(nums.begin(), nums.end(), 0, std::plus<>{}); // Compiles, runs, but not what you want

std::ranges::fold_left(nums, 0, std::plus<>{});          // Ok, result type is double
```

1. *`std::plus` is a class template, `std::plus<>` (which is `std::plus<void>`) is a concrete class,
and `std::plus<>{}` is an object. Function arguments can only be values.*
2. *`std::plus<void>` is special, its call operator is generic - takes any two types. It's a "Transparent Operator Functor"*
3. *If I had a magic wand, I would go back to 1998 and make `std::plus` and friends just such functors*


For `fold_left`, the binary operation is not defaulted, so you always need to supply one.

Plug `fold_left` in our example:

```cpp
double sum_fold_left(std::span<double> nums) {
    return std::ranges::fold_left(nums, 0, std::plus<>{});
}
```

It *bascially* compiles to the same assembly as `accumulate`,
with the make-it-even trick present. (It's just `fold_left` [makes one more comparison](https://godbolt.org/z/aofq4Krf1)
before going to the happy case. I'm not sure why.)

What if you're feeling lazy and don't want to supply the `init`?
That's where `fold_left_first` comes in:

```cpp
template< ranges::input_range R, /* binary-op */ F >
constexpr auto fold_left_first( R&& r, F f );
```

Like `fold_left`, its result type depends on `F`, hence `f` cannot be omitted.

Unlike `reduce`, when `r` is empty, it doesn't return `{}` of the result type;
rather, it returns an empty optional. Yep, the return type of `fold_left_first`
is not the result type `U`, but an `std::optional<U>`.

`fold_left_first` uses the first element of `r` as the initial value.
And by doing do, `fold_left_first` calls `f` *one fewer time* than `fold_left` / `reduce` / `accumulate`.
This makes sense for reduction like `max` and `min` which does not have a meaning value for empty ranges.


## std::ranges::reduce?

If `fold_left` is a replacement for `accumulate` (despite not 100% equivalent),
how about `reduce`? Will we get `std::ranges::reduce`?

Well, there is the paper [P2760 - A Plan for C++26 Ranges](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2760r0.html#tier-1),
where it lists `reduce` as `Tier 1` target.
However, `std::reduce` has execution policy support too,
and adding parallelism to range algorithms is a [**big** project](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3179r0.html).

One issue is the explosion of overloads.
All `ranges` algos provide at least two overloads,
one accepting a pair of iterators and the other accepting ranges.
If execution policy support is to be added, the overloads double yet again:

```cpp
template <class It>          auto f(It first, It last);
template <class R>           auto f(R&& range);
template <class E, class It> auto f(E&& policy, It first, It last);
template <class E, class R>  auto f(E&& policy, R&& range);
```

So that's a `x4` for every parallelizable algorithm,
and that's not counting the non-range ones in `std::`, which makes it `x6`.
And if all they do is but to forward to `std::reduce`,
we might as well DIY and use `std::reduce` today.

For all the reasons, I expect we'll stick to `std::reduce` for a while.



# Summary

| Function Template | Order | Initial Value | Result Type | Return Type |
| ---- | ----- | ------------- | ----------- | ----------- |
| `accumulate(I first, I last, T init)` | Left to Right | `init` | `T` | `T` |
| `reduce(I first, I last)` | Arbitrary | `iter_value_t<I>{}` | `iter_value_t<I>` | `iter_value_t<I>` |
| `reduce(I first, I last, T init)` | Arbitrary | `init` | `T` | `T` |
| `fold_left(I first, I last, T init, F f)` | Left to Right | `init` | same as `f(init, *first)` | same as `f(init, *first)` |
| `fold_left_first(I first, I last, F f)` | Left to Right | `*first` (if not empty) | same as `f(init, *first)` | `optional<U>` where `U` is `f(init, *first)` |
