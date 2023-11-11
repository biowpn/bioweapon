---
title: "Round To Multiple"
date: 2023-03-19
---


## Intro

Rounding is one of the recurring tasks.
While there are standard solutions (`std::round` and friends) to "rounding floating-point to whole number",
there is a more general class of rounding problems:

**Round integer N to multiple of integer M.**

For example, given `N = 17` and `M = 10`:

- Rounding up: `20`
- Rounding down: `10`
- Rounding to closest: `20`

These "round-to-multiple" problems arise in many fields.
For example, finance: the price of a security must be multiple of "tick size".


## Problem

Our goal is to implement the following:

- `int round_up(int n, int m)`
  - Return the smallest multiple of `m` that is greater than or equal to `n`

Constraints:
  - `m > 0`
  - The result is representable by the type `int`

Once we have `round_up`, we can implement the following as well:
- `int round_dn(int n, int m)`
  - Return the largest multiple of `m` that is less than or equal to `n`
- `int round_closest(int n, int m)`
  - Return the multiple of `m` that is the closest to `n`


## Solution

Let's look at some existing solutions.


### The StackOverflow Solution 1

The top answer of [this question](https://stackoverflow.com/questions/3407012/rounding-up-to-the-nearest-multiple-of-a-number)
gives two versions. We'll look at the simpler one that works for positive `numToRound` only:

```cpp
int roundUp(int numToRound, int multiple)
{
    if (multiple == 0)
        return numToRound;

    int remainder = numToRound % multiple;
    if (remainder == 0)
        return numToRound;

    return numToRound + multiple - remainder;
}
```

The last expression

```cpp
numToRound + multiple - remainder
```

does the rounding up. It mostly works, but ...

Consider `roundUp(2147483644, 5)` (assuming `int` is 32-bit).

Note that maximum value of signed 32-bit integer is `2147483647`.
When we add `5` to `2147483644`, it overflows. In other words, we have undefined behavior.

The fix is simple - we subtract `remainder` first:

```cpp
numToRound - remainder + multiple
```

Now let's look at the 2nd version, where `numToRound` can be negative:

```cpp
int roundUp(int numToRound, int multiple)
{
    if (multiple == 0)
        return numToRound;

    int remainder = abs(numToRound) % multiple;
    if (remainder == 0)
        return numToRound;

    if (numToRound < 0)
        return -(abs(numToRound) - remainder);
    else
        return numToRound + multiple - remainder;
}
```

Unfortunately, even after the fix, it still overflows.

Consider `roundUp(-2147483648, 5)`.
Applying `abs` to `-2147483648` overflows.

On my machine, this is even "worse" than the first case:

```sh
roundUp(2147483644, 5) = 2147483645    # while UB, the answer seems "correct"?
roundUp(-2147483648, 5) = 2147483645   # UB and wrong answer!
```


### The StackOverflow Solution 2+

Next, let's took at the second answer:

```cpp
int roundUp(int numToRound, int multiple) 
{
    assert(multiple);
    int isPositive = (int)(numToRound >= 0);
    return ((numToRound + isPositive * (multiple - 1)) / multiple) * multiple;
}
```

This is more concise and performant (`if` branches are eliminated; potentially branchless).

However, it also overflows in the `roundUp(2147483644, 5)` case.

On the other hand, this solution handle all negative `numToRound` cases correctly.

There are many solutions of the form:

```cpp
(numToRound + multiple - 1) / multiple * multiple
```

Sufficiently large `multiple` will overflow `(numToRound + multiple - 1)`.


### Alternative Approaches

Let's try "reductionism" - reducing an unsolved problem to a solved problem:

```cpp
int round_up(int n, int m) {
    return std::ceil(double(n) / double(m)) * m;
}
```

I've seen solutions like this in production as well.

Set aside for efficiency considerations (floating conversion and floating math),
this solution will run into different issues when we try to generalize it:

```cpp
int64_t round_up(int64_t n, int64_t m) {
    return std::ceil(double(n) / double(m)) * m;
}
```

Since `double` cannot represent all `int64_t` accurately,
when you feed it a sufficiently large `n`, the result will be wrong.


### Can We Do Better?

A quick revision of integer division in C++ (and C):

> Signed integer division truncates towards zero

Example:
- `7 / 3 -> 2`
- `-7 / 3 -> -2`

Note in the negative case - divide-then-multiply (`n / m * m`) gives us exactly what we want!

When `n > 0`, if `n` is already a multiple of `m`, the result is simply `n`.

Otherwise, `n / m` gets truncate down (per integer division rule),
and we need to compensate by adding *something* *somewhere*.

The previous solutions add `m - 1` (or `m - remainder`) to `n`,
which triggers overflow when `n` is close to maximum.

Therefore, instead we add `1` to `n / m`. And we're done!

```cpp
int round_up(int n, int m) {
    if (n < 0) {
        return n / m * m;
    } else {
        if (n % m == 0) {
            return n;
        } else {
            return (n / m + 1) * m;
        }
    }
}
```

The above can be simplified into:

```cpp
int round_up(int n, int m) {
    return (n / m + ((n > 0) & bool(n % m))) * m;
}
```


### Rounding Down

Rounding down is symmetrical to rounding up:
- If `n > 0`, divide-then-multiply gives us what we want
- If `n < 0`, we subtract 1 from the quotient if `n` is not a multiple of `m`

```cpp
int round_dn(int n, int m) {
    return (n / m - ((n < 0) & bool(n % m))) * m;
}
```


### Rounding to Closest

What about `round_closest`?

The answer in [this StackOverflow Question](https://stackoverflow.com/questions/29557459/round-to-nearest-multiple-of-a-number):

```cpp
((number + multiple/2) / multiple) * multiple;
```

shares similar overflowing issues, and the author pointed it out.

Let's try reductionism again.

We know that the result is one of `round_up` and `round_dn`, whichever is closer to `n`.
So, we can do it this way:

```cpp
int round_closest(int n, int m) {
    auto lo = round_dn(n, m);
    auto hi = round_up(n, m);
    // guaranteed that n is between [lo, hi]
    return (n - lo < hi - n) ? lo : hi;
}
```

This works, with the added benefits that we can control precisely the behavior half-way cases.
The above code rounds the halfway cases up. We can easily tweak it to "round down", "towards zero", or "away from zero".


## Summary

Putting everything together:

```cpp
template<class T>
T round_up(T n, T m) {
    assert(m > T{0});
    return (n / m + ((n > T{0}) & bool(n % m))) * m;
}

template<class T>
T round_dn(T n, T m) {
    assert(m > T{0});
    return (n / m - ((n < T{0}) & bool(n % m))) * m;
}

template<class T>
T round_closest(T n, T m) {
    auto lo = round_dn(n, m);
    auto hi = round_up(n, m);
    return (n - lo < hi - n) ? lo : hi;
}
```
