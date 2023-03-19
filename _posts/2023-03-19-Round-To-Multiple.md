---
title: "Round To Multiple"
date: 2023-03-19
---


## Intro

Rounding is one of the recurring challenges in software engineering.
While there are standard solutions (`std::round` and friends) to "rounding floating-point to whole number" problem,
there is an entirely different class of rounding problems in the wild:

> Round N to multiple of M.

For example, given `N = 17` and `M = 10`:

- Rounding up: `20`
- Rounding down: `10`
- Rounding to closest: `20`

These "round-to-multiple" problems arise in many fields.
For example, finance: the price of a security must be multiple of "tick size" (imposed by the exchange).


## Problem

Our goal today is to implement the following:

- `int round_up(int n, int m)`
  - Return the smallest multiple of `m` that is greater than or equal to `n`
  - Constraints:
    - `m > 0`
    - The result is representable by the type `int`

Once we have `round_up`, we can modify the solution to implement the following as well:
- `int round_dn(int n, int m)`
  - Return the largest multiple of `m` that is less than or equal to `n`
- `int round_closest(int n, int m)`
  - Return the multiple of `m` that is the closest to `n`

We'll talk about floating-point variations (e,g., `double round_up(double n, double m)`) later.


## Solution

I originally tried ChatGPT first, but it gave me `{"success":false,"error":"The answer could not be generated", ...}`.

So let's look at StackOverflow instead (which ChatGPT probably learnt from anyway).


## The StackOverflow Solution 1

The top answer of [this question](https://stackoverflow.com/questions/3407012/rounding-up-to-the-nearest-multiple-of-a-number)
gives two versions. We'll look at the simpler one that works for positive `n` only:

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

does the "rounding up". It mostly works, but ...

Consider `roundUp(2147483644, 5)` (assuming `int` is 32-bit).

Note that maximum value of signed 32-bit integer is `2147483647`.
When we add `5` to `2147483644`, it overflows. In other words, we have undefined behavior.

The fix is simple - we subtract `remainder` first:

```cpp
numToRound - remainder + multiple
```

Now let's look at the 2nd version, where negative `N`s are supported:

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

The handling of negative numbers is here:

```cpp
    if (numToRound < 0)
        return -(abs(numToRound) - remainder);
```

Unfortunately, this could also overflow:

Consider `roundUp(-2147483648, 5)`.
After `abs` it becomes, at least mathematically, `2147483648`, which overflows our poor 32-bit `int`.

On my machine, this is "worse" than the first case:

```sh
roundUp(2147483644, 5) = 2147483645    # while UB, the answer seems "correct"?
roundUp(-2147483648, 5) = 2147483645   # UB and wrong answer!
```


## The StackOverflow Solution 2

Next, let's took at the second most up-voted answer:

```cpp
int roundUp(int numToRound, int multiple) 
{
    assert(multiple);
    int isPositive = (int)(numToRound >= 0);
    return ((numToRound + isPositive * (multiple - 1)) / multiple) * multiple;
}
```

This is more concise and performant (`if` branches are eliminated; potentially branchless).

However, it also overflows in the `roundUp(2147483644, 5)` case, as the expression reduces to:

```cpp
return (2147483644 + 1 * (5 - 1)) / 5 * 5;
    //  ^^^^^^^^^^^^^^^^^^^^^^^^ becomes 2147483648
```

On the other hand, this solution handle all negative `numToRound` cases correctly.


## The StackOverflow Solutions 3+

There are many solutions of the form:

```cpp
(numToRound + multiple - 1) / multiple * multiple;
```

Unlike the first solution, you can't fix it by simply subtracting 1 first.

Unfortunately, I've seen solutions like this in production.


## Can we do better?

A quick revision of integer division in C++ (and C):

> signed integer division truncates towards zero

Example:
- ` 7 / 3 -> 2`
- `-7 / 3 -> -2`

Note particularly on the negative case - it already gives us what we want!
(The same reason why the 2nd StackOverflow solution works for all negative inputs.)

```cpp
int round_up(int n, int m) {
    if (n < 0) {
        return n / m * m;
    }
    ...
}
```

When `n > 0`, if `n` is already a multiple of `m`, the result is simply `n`.

Otherwise, `n / m` gets truncate down (per integer division rule),
and we need to compensate by adding *something* *somewhere*.

The solutions above add `m - 1` (or `m - remainder`) to `n`,
which triggers overflow when `n` is close to maximum.

Therefore, instead we add `1` to `n / m`. And we're done!

```cpp
int round_up(int n, int m) {
    if (n % m == 0) {
        return n;
    }
    if (n < 0) {
        return n / m * m;
    } else {
        return (n / m + 1) * m;
    }
}
```

which can be simplified into:

```cpp
int round_up(int n, int m) {
    return (n / m + (n > 0) & (n % m)) * m;
}
```
