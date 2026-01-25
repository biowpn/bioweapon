---
title: "Structured Binding Upgrades in C++26"
date: 2024-12-03
---


## Intro

[Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding) was first introduced in C++17.
One main use case is decomposing key and value cleanly in a range-based for loop:

```cpp
// C++17 and later
for (const auto& [k, v]: map) {
    ...
}

// C++14 and before
for (const auto& item: map) {
    const auto& k = item.first;
    const auto& v = item.second;
    ...
}
```

Of course, structured binding works in regular declarations too:

```cpp
std::pair<int, int> cord = ... ;
auto [x, y] = cord;
```

But that's basically about it in C++23. There are many things that a normal declaration can do, but a structured binding cannot:

```cpp
constexpr int i = 0;        // Ok
constexpr auto [i, j] = p;  // error: structured binding declaration cannot be 'constexpr'

int x = 0, y [[maybe_unused]] = 0;  // Ok, individual attributes
auto [x, y [[maybe_unused]] ] = p;  // error

if (auto n = f())   { ... }  // Ok, declaration as condition
if (auto [n] = f()) { ... }  // error
```

All of the above are about to change in C++26.




## Under The Hood

The mental model for structured binding is: there is a hidden variable `e`,
whose fields are the names between the `[` and `]` and one-to-one bind to the elements of *something*.

That is:

```cpp
auto& [x, y] = something;
```

translates to

```cpp
auto& __sb = something;
// hereafter every x translates to __sb.x which binds to the 1st element of `something`
// hereafter every y translates to __sb.y which binds to the 2nd element of `something`
```

Importantly, the modifiers to `auto` apply to the hidden variable `__sb`, *not* the names introduced:

```cpp
std::tuple<int&, int&> fn();

auto& [x, y] = fn();  // error: cannot bind non-const lvalue reference to an rvalue
// x and y do not become int&

auto [x, y] = fn();  // ok
// x and y are int&, as determined by the tuple
```

The "bind-to" relationship is like references, but it is not an actual reference.

Much like lambdas, `__sb` is a magic variable whose type can't be spelled, hence `auto` is needed.

`something` can be:

1. An array
2. A tuple-like (this includes `std::tuple`, `std::array`)
3. A struct with all public data members

Case 1 and 3 work out-of-the-box, no further code required.

Case 2 is how we add structured binding support for user defined types,
outside the standard library ones. The customization code goes like:

```cpp
// We want to export just the names, not the age.
class Person {
    std::string first_name_;
    std::string last_name_;
    int age_;

  public:
    // Step 1: Provide getters
    // They can, alternatively, be provided as free functions under the same
    // namespace, being less invasive but more tedious to access the private
    // fields.
    template <std::size_t I, class T>
    constexpr auto&& get(this T&& self) noexcept {
        if constexpr (I == 0) {
            return std::forward<T>(self).first_name_;
        } else if (I == 1) {
            return std::forward<T>(self).last_name_;
        }
    }
};

// Step 2: Provide the number of fields for the binding
template <>
struct std::tuple_size<Person> : std::integral_constant<std::size_t, 2> {};

// Step 3: Provide the types of fields
template <std::size_t I>
struct std::tuple_element<I, Person> {
    using type = std::remove_cvref_t<decltype(std::declval<Person>().get<I>())>;
};
```

As we can see, structured binding is a language feature that is closely tied to a library feature (`std::tuple_size` and `std::tuple_element`).

Let's see some recent developments for structured bindings that will become part of C++26.




## Individual Attributes

Thanks to [P0609 - Attributes for Structured Bindings](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0609r3.pdf),
structured binding identifiers will be able to have individual attributes:

```cpp
auto [x, y [[maybe_unused]] ] = p;  // Ok since C++26, the `maybe_unused` applies to y only
```

Note that we've always had attributes for the structured binding as a whole; that is, for the hidden variable:

```cpp
[[maybe_unused]] auto [x, y] = p;
// which translates to
[[maybe_unused]] auto __sb = p;
```

Individual attributes allow for a more granular control, which is nice.

I can see myself writing more often:

```cpp
auto [it, inserted [[maybe_unused]] ] = map.try_emplace(key, value);
// future or commented out code uses `inserted`
```




## Structured Binding As a Condition

Thanks to [P0963 - Structured binding declaration as a condition](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0963r3.html),
we will be able to use structured binding as the condition:

```cpp
if (auto [n] = f()) { ... }

while (auto [header, body] = receive_packet()) { ... }
```

Along with [P2497](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2497r0.html)
that adds `operator bool` for `std::[to|from]_chars_result`, we can finally write:

```cpp
if (auto [to, ec] = std::to_chars(p, last, 42))
{
    ...
}
```

One thing to note is the sequencing of **decomposing** and **testing** the condition. Which happens first?

Spoiler: the version in the standard draft is: **test, then decompose**. That is:

```cpp
if (auto [a, b, c] = f())
```

is roughly

```cpp
// test-then-decompose
if (auto e = f(); auto [a, b, c] = e)
```

According to the paper, doing the other way around (decompose-then-test) runs into issues,
including read from moved-from object. Although, I doubt there's noticeable difference for most uses.

Also note that the decomposing happens unconditionally; there is no "test, then only decompose if true".

By the way, Clang has implemented this feature since 6.0.0, you can [try it out](https://godbolt.org/z/5x4oq3hK4) today.




## constexpr Structured Binding

With [P2686 - constexpr structured bindings](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2686r4.pdf),
we can finally have `constexpr` structured binding declaration.

Even better, this heroic paper also solved the long-lasting "problem" of forming a `constexpr` reference to a `constexpr` variable:

```cpp
int main() {
    constexpr int   n = 42;
    constexpr auto& r = n;  // error
}
```

This has bugged me once when I was writing a templated library, and the error was buried very deep.

Why doesn't it work? Well, modern Clang provides a nice explanation:

```
<source>:3:21: note: reference to 'n' is not a constant expression
<source>:2:19: note: address of non-static constexpr variable 'n' may differ on each invocation of the enclosing function; add 'static' to give it a constant address
```

Currently `constexpr` reference is treated like `constexpr` pointer. It requires constant address,
which function-scope variables don't have, even if they are `constexpr` - 
only their *value* is a compile-time constant, not their *location*.

And the fix would be adding `static`:

```cpp
int main() {
    static    int   n = 42;
    constexpr auto& r = n;   // ok
}
```

This works, but still leaves a lot to be desired.
For one, static variables have their own woes, being thread-unsafe is one.
More importantly, do we really care about the actual address of `n` at each invocation?
Does the program behave any differently when `n` takes a different specific address? Should it?

Structured binding for tuple-like types (case 2) are sort of like references,
so this issue would arise for `constexpr` structured bindings as well.
We care even less for the addresses of binding identifiers, since we can't even access the hidden underlying variable to begin with!

The solution proposed by P2686 is very interesting. It introduced the notion of **Symbolic Addressing**.
What this fancy term means is: for a variable to be *constexpr-referenceable*, it doesn't need to have a constant absolute address;
its address just needs to be **constant relative to the stack frame**.

Hence, the following will *just work* in C++26:

```cpp
int main() {
    constexpr int   n = 42;
    constexpr auto& r = n;   // Ok

    constexpr auto [a] = std::tuple(1);  // Ok
    static_assert(a == 1);               // Ok
}
```

I want to emphasize again that the P2686 is **heroic**.
It chose the hardest path and solved the problem (and a sister problem) beautifully.
It could have chose a much easier way, for example by making `constexpr` structured binding only work with `static` variables.
It could still title "`constexpr` structured bindings", just a much nerfed one.

It is just rare to see a paper go beyond "minimum diff" to bringing all out, and even rarer, get accepted.




## Structured Binding Can Introduce a Pack

Speaking of heroic papers, here's my favourite one: [P1061 - Structured Bindings can introduce a Pack](https://isocpp.org/files/papers/P1061R10.html).

I've talked about it [before](https://biowpn.github.io/bioweapon/2023/11/11/printing-aggregates.html).
The [r/cpp](https://www.reddit.com/r/cpp/) community also has [a lot of interests](https://www.reddit.com/r/cpp/search/?q=P1061) in this paper.

This single feature:

```cpp
auto [...xs] = p;
```

**opens a new world of possibilities**. It makes turning a struct to a tuple seamlessly:

```cpp
template <class T>
auto tie_as_tuple(T& x) {
    auto& [...xs] = x;
    return std::tie(xs...);
}
```

This forms the basis of many existing reflection libraries, all of which have to do a lot of [hacks](https://github.com/boostorg/pfr/blob/develop/include/boost/pfr/detail/core17_generated.hpp)
due to the lack of P1061.

`T` doesn't even need to be a struct. Since this is structured binding we're talking about,
we can extend support to any class type by specializing `std::tuple_size` and `std::tuple_element` and providing `get`,
as illustrated earlier in this article. It's just that structs (or, more accurately, [aggregates](https://en.cppreference.com/w/cpp/language/aggregate_initialization))
work out-of-the-box.

By the way, I'm fully aware that P2996 - the big reflection paper exists. I'm just not very confident that it'll make it to C++26.
With P1061 accepted, a lot of the existing problems can already be solved, and I'll gladly take a win when I see it.

P1061 also aimed high. It attempted to solve a new and very hard problem - introducing packs outside templates:

```cpp
struct Point {
    int x, y;
};

int main() {
    Point p{3, 4};

    auto [...cords] = p;  // Hello, I'm a pack!

    auto dist_sq = (cords * cords + ...);  // Fold expressions must work
    static_assert(sizeof...(cords) == 2);  // As must the sizeof... operator
}
```

Up until R9 of the paper, P1061 introduced the notion of *implicit template region*.
The basic idea is, a structured binding pack outside template makes the nearby region become templated-ish.

According to the author:

> ... in our estimation, this functionality is going to come to C++ in one form or other fairly soon

However, the "implicit template region" strategy eventually got dropped, most likely due to implementation concerns,
such as [here](https://www.reddit.com/r/cpp/comments/1dsjbpr/comment/lb632jw).

What actually made it to C++26 is a nerfed version of the paper:
structured binding can introduce a pack, **but only in templates**.

```cpp
struct Point {
    int x, y;
};

int main() {
    Point p{3, 4};

    auto [...cords] = p;  // No can do
}
```

Which is still infinitely better than not having the feature.
If you *really* want structured binding packs outside templates, you can explicitly introduce a templated region:

```cpp
struct Point {
    int x, y;
};

int main() {
    Point p{3, 4};

    [&](auto& _p) {
        auto [...cords] = _p;  // Yes sir
    }(p);
}
```

- Not coincidentally, this is how the implicit templated region roughly works

Of course, I would rather have the full thing, and I hope to see it in the future. Maybe.



## Summary

Structured binding receives a lot of powerful upgrades in C++26. With reflection on the way,
C++26 is going to be very exciting*.

`*` *After patiently waiting for the structured binding upgrades being implemented ...*
