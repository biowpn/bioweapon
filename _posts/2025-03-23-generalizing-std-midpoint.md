---
title: "Generalizing std::midpoint"
date: 2025-03-23
---


## Intro

`std::midpoint` computes the midpoint, or the average, of two numbers.
Why would we need such a one-liner in the standard library? Isn't is just `(a + b) / 2`?

I thought the same thing. That is, until I watched the following talk by Marshall Clow:

- [std::midpoint? How Hard Could it Be?](https://youtu.be/sBtAGxBh-XI)

It was really eye-opening. As it turns out:

`(a + b) / 2` is susceptible to integer overflow, which is undefined behavior for signed integer types
  - In my x86-64 environment, `(1 + INT_MAX) / 2` yields `-1073741824`
  - `a + (b - a) / 2` triggers overflow too, just for different inputs; for example, `a = -1, b = INT_MAX`

You may think, "heh, this example is clearly contrived; my inputs won't be that extreme, so `(a + b) / 2` will do just fine". Well, that's *exactly* the thought behind every integer overflow bug (and there are a lot of them).

- Assumptions are often violated in unexpected ways, because otherwise we would have anticipated and fixed them!

In complex projects, it is not obvious the input leading up to midpoint computation. Any change to the steps prior risks breaking the assumption. For example, a seemingly unrelated change from `int64_t` to `int32_t`, likely for space optimization.

The scariest thing is, this sort of bug is silent; the program continues with the wrong calculation.

I'd like to note that this problem is not specific to `(a + b) / 2`. The built-in arithmetic operators (`+`, `-`, `*`, `/`) share the same overflow problem: the result is not well-defined for all possible input `(a, b)`:

```cpp
INT_MAX + 1    // UB! Integer Overflow
INT_MIN - 1    // UB! Integer Overflow
INT_MIN * -1   // UB! Integer Overflow
INT_MIN / -1   // UB! Integer Overflow
```

But `midpoint(a, b)` is different; it **is** well-defined for all possible input `(a, b)`. This is because the result `m` is always within a and b (inclusive), and since `a` and `b` are representable within range of the type (because, well, they exist), so must be `m`.

The correct implementation of `midpoint` (for integers), taken from Marshall's talk, looks like:

```cpp
#include <type_traits>

template <class Integer>
constexpr Integer midpoint(Integer a, Integer b) noexcept {
    using U = std::make_unsigned_t<Integer>;

    int sign = 1;
    U m = a;
    U M = b;
    if (a > b) {
        sign = -1;
        m = b;
        M = a;
    }
    return a + sign * Integer(U(M - m) >> 1);
}
```

This is definitely more involved than `(a + b) / 2`. Essentially, it is a refined form of `a + (b - a) / 2`:

1. Compute `(M - m)`, which is the distance between `a` and `b`
    - If `a` and `b` are unsigned, this result is naturally correct
    - If `a` and `b` are signed, casting a negative value to unsigned is the same as adding `2^N` to it, and since unsigned has modulo-2 arithmetic, the result will also be correct
2. Apply additional `U` cast to `(M - m)` to account for integral promotion (`(unsigned short - unsigned short) -> signed int`)
3. The distance is halved, and now it is within the representable range of possibly **signed** type `Integer`
4. Add this halved distance to `a`, adjusted by sign

The implementation makes clever use of unsigned arithmetic, while handling integral promotion carefully.


## A lerp for Integers

Can we generalize `midpoint` further? How about the quantile? Or an arbitrary k-th percentile?

Why, there is already [`std::lerp`](https://en.cppreference.com/w/cpp/numeric/lerp):

- `lerp(a, b, t)` returns `a + t * (b - a)`

The thing is, `lerp` converts all arguments to floating points and does math in floating point arithmetics.
Conversion between floating points and integers can be lossy since not all integers can be represented by floating points precisely:

```cpp
std::int64_t a = INT64_MAX - 2;
std::int64_t b = INT64_MAX;

std::cout << (INT64_MAX - 1) << "\n";
std::cout << std::midpoint(a, b) << "\n";
std::cout << static_cast<std::int64_t>(std::lerp(a, b, 0.5)) << "\n";
```

Output:

```
9223372036854775806
9223372036854775806
-9223372036854775808
```

We can see that the `lerp`-based midpoint gives the wrong answer.

Can we write a `lerp` for integers, `ilerp`, so that it always gives the correct result?

Further, we add the constraint that `t` must be between 0 and 1 (interpolation only).

- This way, the result is always representable, and our function accepts all possible input values, just like `midpoint`
- That is not to say extrapolation is not useful (it is indeed quite useful)



## API Design

What should our `ilerp` look like?

The straightforward way is copy what `lerp` does - using a third parameter `pos` to indicate the "position" of the output between `a` and `b`:

```cpp
template <class Integer>
constexpr Integer ilerp(Integer a, Integer b, /* ?Type */ pos) noexcept;
```

What should `/* ?Type */` be? If it is floating point like `double`, then we run into the same lossy conversion problem.

We want `pos` expressed in integral form, in some way. But `pos` is conceptually a fraction between 0 and 1, so it can't just be a single integer.

How about two integers then? That is,

```cpp
template <class Integer>
constexpr Integer ilerp(Integer a, Integer b, Integer pos_num, Integer pos_den) noexcept;
/// The indicated position is pos_num / pos_den
```

This can work. The next question is, how do we handle invalid input? What happens if the indicated position (`pos_num / pos_den`) is not between `0` and `1`? What happens if `pos_den == 0`?

One option is to throw our hands up and draw power from UB:

- If the position is not between `0` and `1`, or if `pos_den == 0`, **the behavior is undefined**

This gives maximal implementation freedom, at the cost of, well, more UBs! The nice thing about `midpoint(a, b)` is that is well-defined for **all** `(a, b)`. Can our `ilerp` follow suits?

We could throw exceptions on invalid input. However, exceptions are not always desired. Besides, both `midpoint` (integral) and `lerp` are `noexcept`. We want to make our `ilerp` also `noexcept`.

We could return a special value, such as `0`, to indicate errors, like `atoi` does. However, it has been long established that this is a bad practice; `0` is a possible valid output, and there is no way to check for errors without adding even more complexity such as some separate channels like `errno`.

We could modify the return type to be `optional<Integer>`, or, even better, `expected<Integer, E>`, so that the caller can check for errors. The minor downside is the burden is now shifted to the caller, and the code is not as clean. In some use cases, the caller knows the position is definitely valid - such as `ilerp(a, b, 1, 4)` - but now we need to write `*ilerp(a, b, 1, 4)`.

There is another way. What if we can make `pos` a compile time constant so we can validate it at compile time? This way, on invalid `pos`, the program fails to compile.

Why, we have exactly the construct we need - [std::ratio](https://en.cppreference.com/w/cpp/numeric/ratio/ratio), a compile-time rational number:

```cpp
template <class Integer, std::intmax_t Num, std::intmax_t Den>
constexpr Integer ilerp(Integer a, Integer b, std::ratio<Num, Den> pos) noexcept {
    using ratio = typename std::ratio<Num, Den>::type;
    // std::ratio already checks that Den != 0
    static_assert(std::ratio_greater_equal_v<ratio, std::ratio<0, 1>>, "pos is less than 0");
    static_assert(std::ratio_less_equal_v<ratio, std::ratio<1, 1>>, "pos is greater than 1");

    // Do the work
    ...
}
```

The only downside is, of course, we can't supply runtime positions, which makes it less functional.

Regardless of runtime or compile-time position, the implementation strategy is similar, which we shall see soon.



## Let's Implement It

The naive way is just to copy paste the lerp formula:

```cpp
template <class Integer, std::intmax_t Num, std::intmax_t Den>
constexpr Integer ilerp(Integer a, Integer b, std::ratio<Num, Den> pos) noexcept {
    ... // checks omitted

    return a + (b - a) * Num / Den;
}
```

Obviously this runs into overflow problems, specifically in two places:

1. `(b - a)`
2. `d * Num` where `d` is the difference of `b` and `a`

Nonetheless, it gives us a starting point.

We can borrow a page from `midpoint` to solve (1), computing the distance between `a` and `b`:

```cpp
template <class Integer, std::intmax_t Num, std::intmax_t Den>
constexpr Integer ilerp(Integer a, Integer b, std::ratio<Num, Den>) noexcept {
    ... // checks omitted

    using U = std::make_unsigned_t<Integer>;

    int sign = 1;
    U m = a;
    U M = b;
    if (a > b) {
        sign = -1;
        m = b;
        M = a;
    }

    U d = M - m;

    ...  // TODO
}
```

Now, about the last part, can we just plug the definition in:

```cpp
    return a + sign * Integer(d * Num / Den);
```

What is the type of `d * Num`? Here, `d` is some unsigned type, and `Num` is `intmax_t`, which is signed.

Is the result always unsigned?

Well, assuming `intmax_t` is `long` (on common x86-64 linux), the results are:

| Operand1 Type        | Operand2 Type | (Operand1 * Operand2) Type |
| ---------------      | ------------- | -----------                |
| `unsigned char`      | `long`        | `long`                     |
| `unsigned short`     | `long`        | `long`                     |
| `unsigned int`       | `long`        | `long`                     |
| `unsigned long`      | `long`        | `unsigned long`            |
| `unsigned long long` | `long`        | `unsigned long long`       |

So, the result is sometimes signed, sometimes unsigned.

This is why mixing signed and unsigned type in the same expression is not a good practice!

To address this, we may just let `U` be `uintmax_t`:

```cpp
    using U = std::uintmax_t;
```

We might as well do this, since we're dealing with `std::intmax_t` anyway (from `std::ratio`).

Then we cast `Num` and `Den` to `U` (which is `std::uintmax_t`). We also know that cast doesn't trigger wrap-around because both of them are checked to be non-negative:

```cpp
    U d = M - m;

    U num = Num;  // Ok because Num >= 0, as checked earlier
    U den = Den;  // Ok because Den > 0, as checked earlier
    return a + sign * Integer(d * num / den);
```

Now that both `d` and `num` are unsigned and have higher rank than `int`, `d * num` will definitely be unsigned.

The next problem is, `d * num` still wraps around when it exceeds `UINTMAX_MAX`. We need to solve this.

Note that the product is already of the biggest integer type `uintmax_t`, so we can't side-step this problem by casting it to a bigger type.

- Well, I lied a bit; in reality, `[u]intmax_t` may *not* be the biggest integer
- "What?! But they have 'max' in their name!"
- On recent x86-64 Linux GCC, there is `__uint128_t`, an unsigned 128-bit integer type, but `uintmax_t` is the same as `uint64_t`, both of which are typedefs to `unsigned long`
- "But **Why**?"
- JeanHeyd Meneide wrote [an excellent article](https://thephd.dev/to-save-c-we-must-save-abi-fixing-c-function-abi) explaining this unfortunate fact. In short, it's due to ABI backward compatibility (of course)

And even if it wasn't, say the max integer supported by the implementation is `REAL_MAX_INT`, when the input of our `ilerp` is `REAL_MAX_INT`, we'll run into the same problem.

- The nice thing about `std::midpoint` is, it works on *any* integer type, including `REAL_MAX_INT`. We want to achieve that in our `ilerp` too.



## The BigMul

Since the result of two N-bit integers multiplication is at most 2N-bit, we can use two N-bit integers to represent the result - one for the lower half and the other for the upper half:

```cpp
struct uint128 {
    uint64_t lo;
    uint64_t hi;
};

uint128 big_mul(uint64_t a, uint64_t b);
```

- The name `big_mul` is taken from C#'s `Math.BigMul`, which does basically the same thing.

The general idea is:

1. Split each 64-bit operand into two 32-bit integers
2. Perform 4 separate multiplications
3. Add up the products

We need to make sure there's no overflow in step (3).
Let `U32_MAX` be `2 ** 32 - 1` and `U64_MAX` be `2 ** 64 - 1`, we have:

```
U64_MAX = U32_MAX * U32_MAX + 2 * U32_MAX
```

This suggests we can add two 32-bit values to the product of any two 32-bit values and store the result in a 64-bit integer without overflow.

```cpp
constexpr uint64_t high(uint64_t x) { return x >> 32; }

constexpr uint64_t low(uint64_t x) { return x << 32 >> 32; }

constexpr uint128 big_mul(uint64_t a, uint64_t b) {
    uint64_t t = low(a) * low(b);
    uint64_t s = high(a) * low(b) + high(t);
    uint64_t r = low(a) * high(b) + low(s);
    return {
        .lo = (r << 32) + low(t),
        .hi = high(a) * high(b) + high(s) + high(r),
    };
}
```

We can verify `big_mul` by comparing it with the native 128-bit multiplication:

```cpp
constexpr uint128 big_mul_128(uint64_t a, uint64_t b) {
    __uint128_t res = __uint128_t(a) * __uint128_t(b);
    return {
        .lo = uint64_t(res),
        .hi = uint64_t(res >> 64),
    };
}

constexpr bool operator==(uint128 one, uint128 two) {
    return one.lo == two.lo && one.hi == two.hi;
}

static_assert(big_mul(uint64_t(-3), uint64_t(-4)) == big_mul_128(-3, -4));  // Ok
```

Now comes the *real power* of C++. We can generalize `big_mul` to take *any* unsigned integer type:

```cpp
#include <limits>
#include <type_traits>


template <class T>
concept unsigned_integer = std::is_unsigned_v<T> && !std::same_as<T, bool>;


template <unsigned_integer T>
struct big_int {
    T lo;
    T hi;
};


// Helpers.
template <unsigned_integer T>
constexpr inline auto bits = std::numeric_limits<T>::digits;

template <unsigned_integer T>
constexpr inline auto half_bits = bits<T> / 2;

template <unsigned_integer T>
constexpr T high(T x) {
    return x >> half_bits<T>;
}

template <unsigned_integer T>
constexpr T low(T x) {
    return x << half_bits<T> >> half_bits<T>;
}


// The generic big-integer multiplication.
template <unsigned_integer T>
constexpr big_int<T> big_mul(T a, T b) {
    T t = low(a) * low(b);
    T s = high(a) * low(b) + high(t);
    T r = low(a) * high(b) + low(s);
    return {
        .lo = T(low(t) + (r << half_bits<T>)),
        .hi = T(high(a) * high(b) + high(s) + high(r)),
    };
}
```

We can even verify this generic `big_mul` without implementation-specific `__uint128_t`: just compare `big_mul<uint32_t>` with the 64-bit multiplication!

```cpp
// This:
    big_mul(uint32_t(-3), uint32_t(-4))
// should equal to this:
    uint64_t(uint32_t(-3)) * uint32_t(-4)
```



## The BigDiv

Now that we've solved `d * Num`, we're ready to compute `d * Num / Den`. This time, we're doing division:

```cpp
template <unsigned_integer T>
constexpr T big_div(big_int<T> n, T d);
```

The simplest way to do it is [Long division](https://en.wikipedia.org/wiki/Long_division), the method we learned in primary school.

Here are some more helpers for manipulating `big_int`:

```cpp
template <unsigned_integer T>
constexpr bool get_ith_bit(big_int<T> n, int i) {
    auto part = i < bits<T> ? n.lo : n.hi;
    i = i < bits<T> ? i : (i - bits<T>);
    return (part >> i) & 1;
}

template <unsigned_integer T>
constexpr void set_ith_bit(big_int<T>& n, int i, bool v) {
    auto& part = i < bits<T> ? n.lo : n.hi;
    i = (i < bits<T>) ? i : (i - bits<T>);
    if (v) {
        part |= T(1) << i;
    } else {
        part &= ~(T(1) << i);
    }
}

template <unsigned_integer T>
constexpr big_int<T> left_shift(big_int<T> n, int x) {
    return big_int<T>{
        T(n.lo << T(x)),
        T((n.hi << T(x)) | (n.lo >> T(bits<T> - x))),
    };
}
```

We also need big integer subtraction:

```cpp
// The generic big-integer subtraction.
template <unsigned_integer T>
constexpr auto big_sub(big_int<T> n, T d) -> big_int<T> {
    if (n.lo >= d) {
        return {T(n.lo - d), T(n.hi)};
    } else {
        T borrow = d - n.lo;
        return {T(std::numeric_limits<T>::max() - borrow + 1), T(n.hi - 1)};
    }
}
```

And finally, here is the long-division-based `big_div`:

```cpp
// The generic big-integer division.
// Based on: https://en.wikipedia.org/wiki/Division_algorithm#Integer_division_(unsigned)_with_remainder
template <unsigned_integer T>
constexpr T big_div(big_int<T> n, T d) {
    if (n.hi == 0) { // optimization
        return n.lo / d;
    }

    big_int<T> q{0, 0};
    big_int<T> r{0, 0};

    for (auto i = 2 * bits<T>; i-- > 0;) {
        // Left-shift R by 1 bit
        r = left_shift(r, 1);
        // Set the least-significant bit of R equal to bit i of the numerator
        set_ith_bit(r, 0, get_ith_bit(n, i));
        if (r.hi != 0 || r.lo >= d) { // r >= d
            r = big_sub(r, d);
            set_ith_bit(q, i, 1);
        }
    }

    return q.lo;
}
```

There are more efficient ways to do big integer division. I'll leave it as an exercise for the reader :).

And, of course, we can always "cheat" by using implementation-defined extended integer types like `__uint128_t` and so on, to let the compiler handle the "small" (relatively) integers.



## Putting Everything Together

With our `big_mul` and `big_div` crafted, our `ilerp` is now complete:

```cpp
template <class Integer, std::intmax_t Num, std::intmax_t Den>
constexpr Integer ilerp(Integer a, Integer b, std::ratio<Num, Den>) noexcept {
    using ratio = typename std::ratio<Num, Den>::type;
    static_assert(std::ratio_greater_equal_v<ratio, std::ratio<0, 1>>,
                  "pos is less than 0");
    static_assert(std::ratio_less_equal_v<ratio, std::ratio<1, 1>>,
                  "pos is greater than 1");

    using U = std::uintmax_t;

    int sign = 1;
    U m = a;
    U M = b;
    if (a > b) {
        sign = -1;
        m = b;
        M = a;
    }

    U d = M - m;

    U num = Num;
    U den = Den;
    return a + sign * Integer(big_div(big_mul(d, num), den));
}
```

The working program can be found [here](https://github.com/biowpn/bioweapon/blob/main/codes/ilerp.cpp).

There you go, a fully portable `ilerp`.
