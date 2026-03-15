---
title: "Circular Distance"
date: 2026-03-14
---


## The Problem

Imagine you're developing a messaging system with a 32-bit unsigned sequence number. This sequence number goes up all the way to `2^32 - 1`, and then wraps around to `0`:

```
0, 1, ..., 4294967295, 0, 1, ...
```

Given two sequence numbers, you want to know which one is ahead, and by how much:

```cpp
// Returns:
//   A positive value N if `b` is ahead of `a` by N steps;
//   A negative value -N if `b` is behind `a` by N steps;
//   0 if `a` equals `b`.
int32_t circular_distance(uint32_t a, uint32_t b);
```

- This follows the `std::distance` convention: `distance(a, b) == diff(b, a) == b - a`

Examples:

- `circular_distance(4, 5)` is `1`, since `5` is ahead of `4` by 1 step.
- `circular_distance(5, 4)` is `-1`.
- `circular_distance(4294967295, 0)` is `1`, since `4294967295` is the max of `uint32_t`, the next one wraps around to `0`.
- `circular_distance(0, 4294967295)` is `-1`.
- `circular_distance(0, 2147483647)` is `2147483647`. This is the max ahead, and the max of `int32_t`.
- `circular_distance(0, 2147483648)` is `-2147483648`. This is the max *behind*, and the min of `int32_t`.
- `circular_distance(2147483648, 0)` is `-2147483648`. When the two numbers are exactly `2^31` steps apart, we define the distance to be always `-2^31`.

How would you implement such a function, `circular_distance`?



## The Solution

It may seem tricky at first, since we need to consider integer overflows, the valid representation range of `int32_t`/`uint32_t`, and even perhaps multiple branches to handle different case, and so on. Some strategies may even upcast to `int64_t`, do the math there, and cast back.

As it turns out, there is a surprisingly simple implementation:

```cpp
int32_t circular_distance(uint32_t a, uint32_t b) {
    return b - a;
}
```

That's it! Unsigned subtraction followed by casting!

But why does it work?



## The Math

Let `N` be the mathematical number represented. Casting a `uint32_t` to a `int32_t` has the effect of:
- If `N` is less than `2^31`, the number is unchanged.
- Otherwise, the number becomes `N - 2^32`.

Now, back to `circular_distance`. Let's consider the case where `a <= b`. If the return type was `uint32_t`, then `circular_distance(a, b)` would always return a non-negative value, implying `b` is always ahead of (or equals to) `a`. But if `b` is *too ahead* of `a`, then we treat it as it's *behind* instead:

- **"Too ahead" = "Behind"**

Consider `a = 0, b = 4294967295`. It takes `a` 4294967295 forward steps to reach `b`, but it takes `b` only 1 forward step to reach `a`.

So we need choose some **cutoff**, `k`. If `b` is at least `k` steps ahead, we would consider it **behind** instead:
- If `d = b - a` is less than `k`, the distance is `d`.
- Otherwise, the distance is `d - 2^32`

Where have we seen this before? If `k` happens to be `2^31`, then above is just casting `uint32_t` to `int32_t`!

So really, it is the coincidence that the cut off `k` happens to be the boundary where the second half of `uint32_t` values "map" to the negative half of the `int32_t` space.

What if `a > b`? Well, we have:

```
circular_distance(a, b) == -circular_distance(b, a)
```

- Except for when `a` and `b` are exactly `2^31` apart; then the circular distance is always `-2^31`.

And it still holds!



## The Generalization

Let's say your sequence number has `n` values. And the *max behind* is `m`. The circular distance is:

```
# Pure math operators
circular_distance(a, b) = m + (b - a) % n
```

In our 32-bit integer cases:
- `n = 2^32` (so we don't need to perform the modulo; C++ does it for us!)
- `m = -2^31`

But not all applications need the full representation range of an integer type, and `n` may not be a power of 2.

For example, analogy clock:
- `n = 12`
- `m = -6`

Or some circular buffer index system, where `n` is the size of the buffer. In the general case, we have to implement modulo ourselves.

While the clock numbers may not be subject to overflows, they run into a different pitfall:

- **In C++, built-in modulo rounds towards 0**

So the below implementation is incorrect:

```cpp
int clock_distance(int a, int b) {
    // Wrong when b < a!
    return -6 + (b - a) % 12;
}
```

What we want for circular distance calculation is really *rounds towards negative infinity*, i.e. flooring.

Thankfully, we have a paper [P3724 - Integer division](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p3724r3.html) that proposed various integer division functions with all the different rounding modes, including:

```cpp
  template<class T>
    constexpr div_result<T> div_rem_to_neg_inf(T x, T y);
```

I would be more than glad to see this paper lands in the standard.
