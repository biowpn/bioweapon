---
title: "Arithmetic Types in C++"
date: 2024-08-29
---


## Intro

> *Simple things are always the hardest to explain.*

In C++, **arithmetic types** include **integral types** and **floating-point types**.


## **The** Integer

Let's begin with the most popular integer type, `int`:

```cpp
int i = 0;
```

In C++, `int` is **fixed** in width.
This means that its size cannot change, and there's a limited range of values it can hold.

- Typically, an `int` is 32 bits or 4 bytes, and the range is -2,147,483,648 to +2,147,483,647



## Length Modifiers

Often, a different size of integer is desired:

- A bigger integer to hold a larger range of values
- A smaller integer to be more space efficient

Thankfully, we have the modifiers `long` and `short`:

- `long int`: an integer type whose rank is higher than `int`
- `short int`: an integer type whose rank is lower than `int`
- A plain `long` is the same as `long int`. Similarly, a plain `short` is the same as `short int`

So, is `long int` bigger than `int` and `short int` smaller than `int`? 

This is where things start to get interesting.

Take `long int`. The C++ standard only guarantees that it is *at least as big as* `int`. It does *not* have to be bigger.

In fact:

- On most versions of Windows, `sizeof(long)` is `4`, same as `sizeof(int)`
- On 64-bit Linux distros, `sizeof(long)` is `8`

Note that even when they have the same width, `int` and `long int` **are two different types**. Likewise, `int` and `short int` are always two different types.

- Typically (on both Windows and Linux), a `short int` is 16 bit

What if you want even bigger or smaller integer types? Well, if you want to go bigger, just slap another `long`:

- `long long int`: an integer type whose rank is higher than `long int`
- Typically, `long long int` is 64 bit

How about going smaller?

Now, if a C++ novice is asked this question right after being told about `long long int`, their response is likely:

```cpp
short short int
```

Alas, there is no such thing as `short short int` in C++ (or C). It is spelled

```cpp
signed char
```

Huh? What does `signed` come from and what is `char` doing here? Are we still talking about integers??

I cannot provide a better answer than

> Well, for historical reasons, it is what it is



## Signed-ness Modifiers

All C++ integers are either `signed` or `unsigned`.

- `signed` types can hold negative values
- `unsigned` types can only hold non-negative values

Further, integer arithmetic is different between `signed` and `unsigned`:

- `signed` integer overflow is undefined behavior (read: very bad)
- `unsigned` integer overflow is well-defined - it wraps around
- That does **not** mean `unsigned` types are safer!
  - In fact, I've encountered bugs that could have been avoided were `signed` type used
  - Both `signed` and `unsigned` are useful in different use cases

If we don't specify the signed-ness, it defaults to `signed`. That is:

- `int` is the same as `signed int`
- `long` is the same as `signed long`
- `short` is the same as `signed short`

and so on.

**Except for `char`**.

`signed char` is **not** the same as `char`. In fact, the following are three distinct types:

- `signed char`
- `char`
- `unsigned char`

All three are character types. However, only `signed char` and `unsigned char` are considered standard integer types.

The unfortunate fact is, `char` is overloaded to mean at least three different things.

1. `char` can mean *character*, the basic unit of a string
2. `char` can mean *byte*, the basic unit of raw memory
3. `char` can mean *integer*, as we've seen with `signed char` and `unsigned char`

The direction of C++ seems to be that **`char` should only mean character**.

- Use `std::byte` if you mean a byte
- Use `std::int8_t` if you mean an integer

Although, there is a lot of existing code out there, and `char` is ubiquitous.

Looking back, really:

- `signed char` should have been `short short`
- `unsigned char` should have been `unsigned short short`



## Fixed width integer types

In summary, we have 10 standard integer types: there are 5 ranks, each with `signed` and `unsigned` variants.

The C++ standard only guarantees minimal bit counts and that the each rank is at least as wide as the last:

```
// char:      at least 8 bit
// short:     at least 16 bit
// int:       at least 16 bit
// long:      at least 32 bit
// long long: at least 64 bit

sizeof(char) <= sizeof(short) <= sizeof(int) <= sizeof(long) <= sizeof(long long)
```

- For complete info, refer to [here](https://en.cppreference.com/w/cpp/language/types)

If precise width is demanded, there are **fixed width integer types** at our disposal:

- `int8_t`: exactly 8-bit signed integer
- `uint8_t`: exactly 8-bit unsigned integer
- ... and so on, up to `int64_t` and `uint64_t`

Aside from width precision, these types are more coherent when it comes to naming/spelling. You don't need to worry about `long` or `short` or `char` or the order thereof.

- `long signed long` is legal

With fixed width integers, the only pattern is `[u]intN_t` and that's it!

The inconvenience is that you have to `#include <cstdint>`. That, and technically they are defined in namespace `std`, so the more tedious but correct way to spell them is e.g. `std::int32_t`.

- This matters, because in the future if there's only `import std;` for pulling in the standard library, you need to `std::` prefix to find these types

In reality, the `[u]intN_t` may be (but not required to be) typedefs of the standard integer types. For example:
  - `int32_t` may be defined as `using int32_t = int;`
  - `uint8_t` may be defined as `using uint8_t = unsigned char;`
  - `int64_t` may be `long` on one system, but `long long` on another

That's the whole point of these integer types: *platform-independent* access to fixed width integers.



## Floating-point types

Aside from integral types, **floating-point types** are other category of arithmetic types.

There are three standard floating-point types:

- `float` is *single precision floating-point type*
- `double` is *double precision floating-point type*
- `long double` is *extended precision floating-point type*

Alas, the spelling of these types is, again, less than perfect.

- If `double` is double precision, why is single precision not `single`?
- If `long` is used to denote "higher precision", why is `double` not `long float`?

The short answer is again "for historical reasons, it is what it is".
We just have to get used to it.

Since C++23, there are [fixed width floating-point types](https://en.cppreference.com/w/cpp/types/floating-point), similar to their fixed width integer cousins:

- `float16_t`
- `float32_t`
- `float64_t`
- ...

However, these are *never* the standard floating-point types (`float`, `double`, `long double`). They are (aliases to) *extended floating-point types*.

Finally, floating-point types are inherently signed. There is no `unsigned float`. Nor is there `signed float`.



## Arithmetic Operations

Now comes the hard part.

> Seriously; the previous parts are *easy*

Consider a binary arithmetic operation, `T @ U`, where `T` and `U` denote two arithmetic types. `@` can be `+`, `-`, `*`, and so on.

C++ requires converting `T` and `U` to a common type before carrying out the operation. The main 3 rules are:

1. If at least one of them is floating-point, then the common type is the largest floating-point.
  - `int @ float` => `float @ float`
  - `float @ double` => `double @ double`

2. All `char` and `short` variants are first converted to `int`; this is known as *integer promotion*.
  - `char @ char` => `int @ int`
  - `unsigned short @ unsigned short` => `int @ int`

3. For two integer types with the same signed-ness, the one with higher rank is the common type
  - `int @ long` => `long @ long`
  - `unsigned int @ unsigned long` => `unsigned long @ unsigned long`

Now, when it comes to **mixed signed-ness operation**, such as `unsigned int @ long`, the recommendation is:

- **Don't**.

No, really. The rules for this area are complex and the results can be surprising. Most compilers warn about this sort of operation, for good reasons.

- If you want to challenge yourself, you may take look at this [infographic](https://hackingcpp.com/cpp/lang/usual_arithmetic_conversions.png) from hackingcpp.com

Since C++20, we even introduce [integer comparison functions](https://en.cppreference.com/w/cpp/utility/intcmp):

- `cmp_less(T, U)`
- `cmp_greater(T, U)`
- ...

*Precisely* because the builtin mixed-signed-ness operations are error-prone.



## Afterword

When it comes to basic stuff like integers and floating-points, pretty much everything was inherited from C, which has been for a long while. Some design probably made sense "back then", but there is definitely better design today. Unfortunately, we don't get to do-over; backward compatibility is pivotal to C, and in turn C compatibility is pivotal to C++.
