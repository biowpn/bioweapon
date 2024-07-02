---
title: "A Type for Overload Set"
date: 2024-07-02
---


## Intro

Let's start with a simple task: how do you convert a list of numbers to a list of strings in C++? That is,

```cpp
std::vector<int> numbers {1, 1, 2, 3, 5};
std::vector<std::string> strings;
// Want: ["1", "1", "2", "3", "5"]
```

In most programming languages, this is a one-liner. For example, in Python:

```py
strings = list(map(str, numbers))
```

The `map(...)` part is basically applying an operation (`str`, "to-string") to each element of the range, but does so lazily.
We should be able to achieve the same thing with all the `<ranges>` machinery, right?
Why, there is [std::views::transform](https://en.cppreference.com/w/cpp/ranges/transform_view)
that does *exactly* that. Let's give it a try:

```cpp
auto strings = std::views::transform(numbers, std::to_string);
```

The long qualified name aside, I think this is as readable as it can get.
If you show this piece of code to a C++ novice, or even a coder who has never written C++,
they will have no problem understanding what it does.

Except, of course, the above code doesn't compile. `std::to_string` is [overloaded](https://en.cppreference.com/w/cpp/string/basic_string/to_string) to support multiple types,
and the compiler doesn't know which one to give to `transform`.

Fair enough, how about:

```cpp
auto strings = std::views::transform(numbers, std::to_string<int>);
```

This does not work, because `std::to_string` is not a template.

Ok, how about we force the overload resolution by explicitly casting to function pointer:

```cpp
auto strings = std::views::transform(numbers, (std::string(*)(int))std::to_string);
```

This ugly hack seems to work, except it's undefined behavior, since you shall not take the address of `std` functions.

The solution is using a lambda:

```cpp
auto strings = std::views::transform(numbers, [](int num){ return std::to_string(num); });
```

What we did above is creating a unary functor (the lambda) that does nothing but forwarding its argument
to another unary function (`std::to_string`), only because we can't directly supply the said unary function.

**Even though we can**, had we written a raw for loop:

```cpp
for (auto num : numbers) {
    auto str = std::to_string(num);  // It just works
    strings.push_back(std::move(str));
}
```


### Another Case

Let's consider another task: compute the minimum over a range of numbers plus a special value, say `INT_MAX`.

This is basically a fold operation where the reduction is `min` and the initial value is `INT_MAX`.

Let's try [fold_left](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left):

```cpp
// `numbers` is std::vector<int>
auto min_v = std::ranges::fold_left(numbers, INT_MAX, std::min);
```

Alas, the above *almost* works, except of course `std::min` has to be wrapped in a lambda:

```cpp
auto min_v = std::ranges::fold_left(numbers, INT_MAX, [](auto const& one, auto const& two){ return std::min(one, two); });
```

Again, the lambda does nothing but forwarding its arguments to another binary function.


### Yet Another Case

In C++20 we got [`std::bind_front`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0356r5.html),
which allows for a terse syntax for partial function application:

```cpp
struct Bar {
    int foo(int);
};

Bar bar;

auto f = std::bind_front(&Bar::foo, bar);
f(1); // same as bar.foo(1);
```

This is pretty neat and clean (and beats the old `std::bind` since we don't need the placeholders `_1` etc.).

Alas, it only works if `foo` is a single regular member function.
It breaks as soon as `foo` is overloaded:

```cpp
struct Bar{
    int foo(int);
    int foo(int) const;
};

Bar bar;

auto f = std::bind_front(&Bar::foo, bar);
// error: no matching function for call to 'bind_front(<unresolved overloaded function type>, Bar&)'
```

Or templated:

```cpp
struct Bar{
    template <class T>
    int foo(T);
};

Bar bar;

auto f = std::bind_front(&Bar::foo, bar);
// error: no matching function for call to 'bind_front(<unresolved overloaded function type>, Bar&)'
```

Or even simply with default arguments:

```cpp
struct Bar{
    int foo(int, int a2 = 0);
};

Bar bar;

auto f = std::bind_front(&Bar::foo, bar);  // The binding works ...
f(0);     // error: no type named 'type' in 'struct std::invoke_result<int (Bar::* const&)(int, int), const Bar&, int>'
f(0, 0);  // Ok
```

In all of the above cases, we have to resort to lambdas:

```cpp
auto f = [&](int num) { return bar.foo(num); };
```

But then, that sort of defeats the purpose of `std::bind_front` - replacing ad-hoc lambdas for use cases as such.
Because outside from contrieved examples, real world partial functions have to deal with arguments perfect forwarding,
return value perfect forwarding, `noexcept`-ness preservation, etc. We'll see an example later.


## Let's Get Down to **invoke**

Since C++17, we have `std::invoke`, which is a generalization that unifies calling callables.
Now, a natural question is, can `std::invoke` really call *anything*? That is, given `f(arg)` is well-formed, is `std::invoke(f, arg)` always well-formed?

The answer is No, again due to function overloading:

```cpp
void f(int);       // (1)
void f(int, int);  // (2)

f(42);               // Ok, select (1)
std::invoke(f, 42);  // Error
```

This example is especially dissatisfying. From the callsite `invoke(f, 42)`,
there is clearly only one `f` that makes `f(42)` well-formed. And `invoke` knows its arguments too.
We even have `concepts` which are supposed to help with overload resolution.
Alas, the C++ type system does not work like this.



## So ... how does it work?

The crux of the problem, where all the examples above share in common,
is that **C++ does not have a type for overload sets**.

Basically, this doesn't work:

```cpp
void f(int);
void f(int, int);
// `f` is an overload set with 2 members

using FF = decltype(f);  // error! overload set has not type

auto g = f;  // error! cannot deduce the type of `g`
```

The following doesn't work either, because constraints are only checked *after* the type is deduced:

```cpp
std::invocable<int> auto g = f;  // error! cannot deduce the type of `g`
```

All the `<ranges>` algorithms, `std::bind_front`, and `std::invoke`, *require* their arguments to be "typed".
You can't pass something that does not have a type to a function!

And lambdas work *precisely* because they have a type (even though the type cannot be spelled). Also, they can implicitly capture overload sets.

This is part of the reason why lambdas are everywhere.
Without them, we basically can't work with `<ranges>` or APIs that accept callables.

The fact that we *need* lambdas to do even the simplest tasks is just ... not nice.



## Hope on the Horizon

There is a recent proposal that aims to address this exact issue: [P3312 - Overload Set Types](https://wg21.link/p3312).
It basically proposes to make the above work:

```cpp
void f(int);
void f(int, int);
// `f` is an overload set with 2 members

using FF = decltype(f);  // Proposed: `FF` is a unique type for overload set `f`

auto g = f;  // Proposed ok, `g` has the same type as `FF`

// Now `g` can be passed around like any other object
```

We can somewhat emulate the paper today with a macro, `OVERLOAD`, which is just a shortcut for creating an ad-hoc lambda:

```cpp
// noexcept-ness preversation not handled
#define OVERLOAD(fun)                                                          \
    [](auto&&... args) -> decltype(auto) {                                     \
        return fun(std::forward<decltype(args)>(args)...);                     \
    }
```

This works for free functions and function templates:

```cpp
void f(int);
void f(int, int);

auto g = OVERLOAD(f);  // Ok; under the hood it's just your friendly lambda!
g(42);      // same as f(42)
g(42, 42);  // same as f(42, 42)
```

but not for member functions:

```cpp
struct Bar{
    int foo(int);
    int foo(int) const;
};

Bar bar;

auto f = std::bind_front(OVERLOAD(&Bar::foo), bar);
f(42);  // error: no matching function for call to 'Bar::foo(const Bar, int)'
```

The compiler is complaining that we attempted to form a call `(&Bar::foo)(bar, 42)`, which is not how member functions are called. We need to write `bar.foo(42)` instead.
`std::invoke` is supposed to unify the syntax down to `invoke(&Bar::foo, bar, 42)`,
but ... that's the very thing we are trying to make work!

We have to prepare a separate macro `OVERLOAD_MF` to handle overloaded member functions:

```cpp
#define OVERLOAD_MF(fun)                                                       \
    [](auto&& self, auto&&... args) -> decltype(auto) {                        \
        return (std::forward<decltype(self)>(self).fun)(                       \
            std::forward<decltype(args)>(args)...);                            \
    }
```

And use it as

```cpp
struct Bar {
    int foo(int);
    int foo(int) const;
};

Bar bar;

auto f = OVERLOAD_MF(Bar::foo);  // Note: without the &
std::invoke(f, bar, 42);  // Ok
```

I do not know of a way to unify `OVERLOAD` and `OVERLOAD_MF` into one, in the general case.

Even then, our `OVERLOAD*` macros only handle the calling part. There is another special ability that overload sets have:
they can "collapse" into a concrete function pointer when given *contextual type information*:

```cpp
void f(int);       // (1)
void f(int, int);  // (2)

auto pf = static_cast< void(*)(int) >(f);  // Ok, address of (1)
```

Can our `OVERLOAD` macros do that?

```cpp
auto g = OVERLOAD(f);
auto pg = static_cast< void(*)(int) >(g);  // (A)
```

Unfortunately, (A) does not work. But, `g` is a capture-less generic lambda, so it should be able to convert to function pointers, right?

The problem is, we cannot find a set of template parameters that specializes `g`'s `operator()` to be `void(int)`,
since we defined it as `(auto&&... args)`. For any function pointer it can convert to, all parameter types must be references; for instance, `void(int&&)`.
It *would* work had we defined `g` as `(auto... args)`, but that means we are passing every argument by value.

In short, lambdas can help us, but only to some extent. Which, for most use cases, will *probably* be enough.



## Conclusion

A type for overload set as proposed in [P3312 - Overload Set Types](https://wg21.link/p3312) would be an exciting addition to the language.
That said, given how complex this topic (overloading) is, we probably won't see the feature in C++ any time soon.
Meanwhile, we just have to keep in mind that lambdas are a good and powerful tool;
and whenever you see `<unresolved overloaded function type>` errors, likely a lambda can help you out.
