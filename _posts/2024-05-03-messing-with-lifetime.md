---
title: "Messing with lifetime"
date: 2024-05-03
---


## Intro

Consider the following code:

```cpp
struct Point { int x, y; };

void foo(unsigned char* buf, size_t len) {
    assert(len == sizeof(Point));
    Point* p = reinterpret_cast<Point*>(buf);
    if (p->x == 0) {
        // ...
    }
}
```

`foo` gets a sequence of bytes, probably over the network or from disk;
it wants to interpret these bytes as an object of some POD type `Point`.

I believe many have written code similar to above; I know I have.
And there's nothing wrong with it.

... Except the C++ standard says the code has undefined behavior.
And it has everything to do with object lifetime.



## But Why?

First thing first, what's the problem with the introduction code?

The short answer is: there is no `Point` object living in `[buf, buf + len)`,
yet `foo` attempts to access one as if there was.

This is similar to the "use-after-free" error (except the other way around),
in that the underlying problem is the same - **accessing an object outside its lifetime**.

Ok, how do we fix it?

One way is to copy the bytes out:

```cpp
void foo(unsigned char* buf, size_t len) {
    assert(len == sizeof(Point));
    Point point;                    // a Point is created
    std::memcpy(&point, buf, len);
    if (point.x == 0) {             // Ok
        // ...
    }
}
```

But this involves extra copying, so it is not optimal.
For small struct like `Point`, this copying doesn't matter;
in real use cases, the struct may be much bigger,
and one may skip processing by just looking at a few bytes at the start
(e.g., checking message header and skip uninterested types).

Is there an way to do this without copying? In other words, can we create a `Point` in-place?

The answer is yes, with [std::start_lifetime_as](https://en.cppreference.com/w/cpp/memory/start_lifetime_as):

```cpp
void foo(unsigned char* buf, size_t len) {
    assert(len == sizeof(Point));
    Point* p = std::start_lifetime_as<Point>(buf);
    if (p->x == 0) {
        // ...
    }
}
```

`std::start_lifetime_as` is one of the new tools that allows explicit lifetime management,
which is a fancy way of saying "messing with lifetime".

Let's investigate.



## What does `new` do?

A classic question is: what is the difference between `new` and `malloc` in C++?

The textbook answer goes like: `new` calls the constructor, while `malloc` does not.
"new = malloc + constructor" has been my mental model for a long time.

Only recently did I find out that there is a **third** thing that `new` does:
`new` **starts the lifetime** of the object it constructs.
This act runs no code; instead it updates the compiler's bookkeeping.

So, my new mental model is: `new` does three things, in this order:

1. Allocate storage
2. Call constructor
3. Start lifetime

Of course, there is placement new which does only 2 and 3.
Nonetheless, all variations of `new` call constructor.
In the introduction cast-bytes-as-POD use case,
we don't want to run constructor to reset the very data we're supposed to read from!

And this is exactly the gap `std::start_lifetime_as` fills: it does only 3, Start lifetime.



## Can this whole process be automatic?

`std::start_lifetime_as` solves the cast-bytes-as-POD use case, and it makes sense.
But there is so much `reinterpret_cast` and C-style cast code in existence to perform the same task,
and they don't just suddenly become valid with `-std=c++23`.
Effort must be spent to migrate those casts to `std::start_lifetime_as` case by case:
not every `reinterpret_cast` is meant to start lifetime.
Also, `start_lifetime_as` needs to be taught (even it's easy to learn).
The overall cost to the user base is not small.

Some may question: is using `reinterpret_cast` as it is today even a problem to begin with?
Compilers have been generating the expected code for decades.
They definitely recognize this pattern since it's common enough.

The issue with `reinterpret_cast` is: *it is overpower*.
It allows you to do many casts, some of which are dangerous:

```cpp
void foo(unsigned char* buf, size_t len) {
    auto ptr = reinterpret_cast<std::string*>(buf);
    // random bytes are unlikely a valid std::string;
    // accessing through ptr is very dangerous,
    // especially if the bytes came from an untrusted source
}
```

At least `std::start_lifetime_as` requires that `T` is trivially-copyable,
so it offers *some* guards compared to using raw `reinterpret_cast`.

Ok, how about making `reinterpret_cast` automatically perform `start_lifetime_as`,
under the same constraint that `T` is trivially-copyable,
so existing code would become valid without any change?

**\<imagination\>**

A `reinterpret_cast<T*>(p)` where 
- `T` is trivially-copyable;
- `p` is `char*`, `unsigned char*`, or `std::byte*` (aka the blessed three), or any cv versions of them;
- `[p, p + sizeof(T))` is valid range

is equivalent to `std::start_lifetime_as<T>(p)` if there is no object within `[p, p + sizeof(T))`.

**\</imagination\>**

One obvious downside is, currently no `cast` of any sort starts lifetime,
so this hypothetical power means some consistency is lost.

That aside, the real difficulty is:
how can the compiler tell whether "there is no object within `[p, p + sizeof(T))`"?
This problem is in general undecidable, since one can create objects conditionally or from another translation unit.

How about only allow `const` uses (`reinterpret_cast<const T*>(p)`),
so that even there are objects in `[p, p + sizeof(T))`, their internals won't meddled with?
In other words, we just want a read-only view of the bytes as if they form a `Point`.
Could it work?

I do not know.

I do believe that there are cleverer people
who could come up with wordings to make `reinterpret_cast` *just work* in this case.
Again, it already works in practice!



## Some Random Thoughts

In general, there are three things when it comes to a piece of C++ code:

1. What the **standard** says should happen
2. What the **users** think will happen
3. What actually happens (i.e., what the **compilers** do)

And the overall situation is more or less "democratic":

- If 1 and 2 agree but not 3, then it's a compiler bug and should be fixed
- If 1 and 3 agree but not 2, then the user should correct their understanding

But what if 2 and 3 agree but not 1? Maybe it's a flaw in the standard?

The case presented in this post, falls into this category.
As does the usage of `malloc` before [P0593](https://wg21.link/P0593):
accessing an object through the pointer returned by `malloc` (and `mmap` and ...) had been UB,
but in practice `malloc` has been working since 1970s:

```c
struct X { int a, b; };
X *make_x() {
    X *p = (X*)malloc(sizeof(struct X));
    p->a = 1;  // before P0593: UB, no X lives at p
    p->b = 2;  // before P0593: UB, no X lives at p
    return p;
}
```

P0593 solves the issue by giving `malloc` and a few other functions special powers so they won't run into lifetime issues.
This is the right approach, as you really can't expect everyone to change their `malloc` to

```cpp
X *p = std::start_lifetime_as<X>( malloc(sizeof(struct X)) );
```

even though the above would work without special-casing `malloc` and others.

Alas, when it comes to user-provided storage, be it custom memory allocators or the casting-bytes-to-POD use case outlined in this post,
every use case must be coated with `start_lifetime_as`. P0593 explicitly says so:

> Note that a pointer reinterpret_cast is not considered sufficient to trigger implicit object creation.

I only wish it was.



### Afterword

Lifetime is a very complex topic, but at the same time it is so fundamental to the language
that it involves everyone. As an average user, there is quite a lot to learn if we want to
write both standard-conforming and maximally performant code. Hopefully, the future version of C++
will make writing such code more accessible.

Recommended materials:
- [This talk by Robert Leahy](https://youtu.be/pbkQG09grFw)
- [P0593 - Implicit creation of objects for low-level object manipulation](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0593r6.html)
- [An (In-)Complete Guide to C++ Object Lifetimes](https://www.jonathanmueller.dev/talk/lifetime/)
