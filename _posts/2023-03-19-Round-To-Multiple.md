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

I originally tried ChatGPT first, but it is not yet available in my region. So let's look at StackOverflow instead (which ChatGPT probably learned from anyway).


### The StackOverflow Solution 1

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

Now let's look at the 2nd version, where negative `N`s are supported.

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


### The StackOverflow Solution 2

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


### The StackOverflow Solutions 3+

There are many solutions of the form:

```cpp
(numToRound + multiple - 1) / multiple * multiple;
```

Unlike the first solution, you can't fix it by simply subtracting 1 first.

Unfortunately, I've seen solutions like this in production.


### Alternative Approaches

Let's consider "reductionism" - the art of reduce an unsolved problem to a solved problem:

```cpp
int round_up(int n, int m) {
    return std::ceil(double(n), double(m)) * m;
}
```

I've seen this form in production as well.

Set aside for efficiency considerations (floating conversion and floating math),
this solution will run into the same overflow issues when we try to generalize it:

```cpp
int64_t round_up(int64_t n, int64_t m) {
    return std::ceil(double(n), double(m)) * m;
}
// double cannot represent all int64_t accurately
```


### Can We Do Better?

A quick revision of integer division in C++ (and C):

> signed integer division truncates towards zero

Example:
- `7 / 3 -> 2`
- `-7 / 3 -> -2`

Note particularly on the negative case - divide-then-multiply (`n / m * m`) gives us exactly what we want!

(The same reason why the 2nd StackOverflow solution works for all negative inputs.)

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
    return (n / m + (n > 0) & (n % m)) * m;
}
```


### Rounding Down

Rounding down is largely symmetrical to rounding up:
- If `n > 0`, divide-then-multiply gives us what we want
- If `n < 0`, we subtract 1 from the quotient if `n` is not a multiple of `m`

```cpp
int round_dn(int n, int m) {
    return (n / m - (n < 0) & (n % m)) * m;
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
We know that the result is one of the results from `round_up` and `round_dn`, whichever is closer to `n`.

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
The above code rounds the halfway cases up.
We can easily tweak it to achieve "round down", "towards zero", or "away from zero".


## Afterwords

Putting everything together:

```cpp
template<class T>
T round_up(T n, T m) {
    assert(m > T{0});
    return (n / m + (n > T{0}) & (n % m)) * m;
}

template<class T>
T round_dn(T n, T m) {
    assert(m > T{0});
    return (n / m - (n < T{0}) & (n % m)) * m;
}

template<class T>
T round_closest(T n, T m) {
    auto lo = round_dn(n, m);
    auto hi = round_up(n, m);
    return (n - lo < hi - n) ? lo : hi;
}
```

Perhaps we should consider standardizing these functions?
The seemingly-trivial [midpoint](https://en.cppreference.com/w/cpp/numeric/midpoint) is standardized.
I recommend the talk by Marshall Clow [std::midpoint? How Hard Could it Be?](https://youtu.be/sBtAGxBh-XI),
which inspires me to pay more attention on integral operations.
