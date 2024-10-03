---
title: "Template Best Practices"
date: 2024-09-30
---


## Function templates are already **inline**

***Note**: after this post is published, Jonathan Wakely points out [here](https://www.reddit.com/r/cpp/comments/1fv14b7/comment/lq3t1x4) that **this is wrong**. "(at least with GCC) `inline` keyword is not redundant there, and does affect inlining decisions".*
*A corrected advice would be: You can omit `inline` for function templates in that they won't cause you compile errors when used in multiple translation units, but they are still not the same as actual `inline` functions.*

This

```cpp
template <class T>
inline T max(T a, T b);
```

is exactly the same as

```cpp
template <class T>
T max(T a, T b);
```

There is no need to slap `inline` onto your function templates. They are already implicitly so.

On the other hand, the `static` keyword cannot be omitted:
- Inside a class definition, `static` means "exists independent of any instance"
- In file scope, `static` means "only visible within the current translation unit"

Also note, for functions and function templates, `constexpr` implies `inline` too:

```cpp
constexpr inline int foo();
//        ^^^^^^
//        This is redundant
```


## Let Call Arguments Be Deduced

I believe all of us have encountered the following error at some point:

```sh
error: no matching function for call to 'max(unsigned int&, int)'
    9 |     std::max(n, 0);
      |     ~~~~~~~~^~~~~~
```

The two usual ways to fix it are:

```cpp
std::max<int>(n, 0);       // (1) Specify template arguments; implicit conversion occurs
std::max(n, (unsigned)0);  // (2) Cast arguments explicitly; no implicit conversion
```

**Prefer (2) over (1)**. The rule of thumb is, **let call arguments be deduced**.

- For this specific case, it's even better to use literal suffix: `std::max(n, 0u)`

This also applies to API design of function templates:

**Put call argument types at the end of the template parameter list.**

```cpp
// Good: only need to specify return type:
//     divide<double>(x, y)
template <class R, class T1, class T2>
R divide(T1 a, T2 b);

// Bad: need to specify everything:
//     func<int, int, double>(x, y)
template <class T1, class T2, class R>
R divide(T1 a, T2 b);

// Evil:
template <class T1, class R, class T2>
R divide(T1 a, T2 b);
```


## Template Parameters Naming

Template parameters should be capitalized.

When you have just one generic type, name it `T`.

When you have two generic types, they can be named `T` and `U`:

```cpp
template <class T, class U>
void convert(T const& from, U& to);
```

Or `T1` and `T2`:

```cpp
template <class T1, class T2>
auto max(T1 a, T2 b);
```

Alternatively, template parameter names can match argument names or have specific meaning:

```cpp
template <class From, class To>
void convert(From const& from, To& to);

template <class InputIt, class OutputIt>
OutputIt copy(InputIt first, InputIt last, OutputIt out);
```

Non-type Template Parameters should match their class name or match their specific meaning too:

```cpp
enum class color { red, green, blue };

template <color C> void foo();
// or
template <color Color> void foo();

template <class T, std::size_t N>  // N for number
class array;
```

Finally, parameter packs should be in plural forms (i.e., end with `s`):

```cpp
template <class ...Args>
void foo(Args&&...);

template <class ...Ts>
struct type_sequence;

template<class T, T... Vs>
struct value_sequence;
```


## Do Not Specialize Function Template

If you are writing a library and need to provide customization point, use **class template**:

```cpp
/* Primary template, default implementation */
template <class T>
struct string_converter {
    T scan(std::string_view s) const {
        T a;
        std::from_chars(s.begin(), s.end(), a);
        // error checking omitted
        return a;
    }
};

/* Specialization for bool */
template <>
struct string_converter<bool> {
    bool scan(std::string_view s) const {
        if (s == "true") return true;
        if (s == "false") return false;
        throw std::runtime_error("bad bool: " + std::string(s));
    }
};
```

and then provide a wrapper function template:

```cpp
template <class T>
T from_string(std::string_view s) {
    string_converter<T> conv;
    return conv.scan(s);
}
```

Users make `from_string` work with their custom types by specializing `string_converter`:

```cpp
// Good: string_converter is the intended customization point
template <>
struct string_converter<UserType> { /* ... */ };
```

instead of specializing `from_string` directly:

```cpp
// Bad: specialize function template
template <>
UserType from_string<UserType>(std::string_view s) {
    /* ... */
}
```

The rule of thumb is, **do not specialize function templates**. But why?

First off, function templates cannot be partially specialized:

```cpp
// Error: function partial specialization is not allowed
template <class E>
std::vector<E> from_string<std::vector<E>>(std::string_view s) {
    /* ... */
}
```

but class templates can:

```cpp
// Ok
template <class E>
struct string_converter<std::vector<E>> {
    /* ... */
};
```

While function templates can be overloaded:

```cpp
// Primary template
template <class T>
T from_string(std::string_view s);

// Ok; this is a different template, overloading the name `from_string`
template <class E>
std::vector<E> from_string(std::string_view s);
```

they may be more awkward to use:

```cpp
// Error: call of overloaded 'from_string' is ambiguous
auto ints = from_string<std::vector<int>>(s);

// Manual overload resolution required
auto fn = static_cast< std::vector<int>(*)(std::string_view) >(from_string);
auto ints = fn(s);
```

On the other hand, with class template approach, `from_string<std::vector<int>>(s)` just works.

There are other reasons why specializing function templates is considered bad practice.

- Function template specialization does not participate in overload resolution. This may lead to surprising behavior.
- I recommend a read to this article by Herb Sutter: [Why Not Specialize Function Templates?](http://www.gotw.ca/publications/mill17.htm)

Lastly, definitely don't do this:

```cpp
// Some old (pre-C++20) code may do this:
template <>
void std::swap(UserType& one, UserType& two) noexcept {
    // ...
}
// Illegal since C++20
```

Use a `friend` function instead:

```cpp
class UserType {
  public:
    friend void swap(UserType& one, UserType& two) noexcept {
        // ...
    }
};
```


## Pass Callables by value or by reference?

Suppose I have a function `g` that takes a (templated) callable `f`. Should it accept `f` by value or by reference?

```cpp
template <class F>
void g(F f);      // (1) by value

template <class F>
void g(F&& f);    // (2) by reference
```

Looking at the C++ standard library, in some places such as `std::invoke`, callables are passed by reference:

```cpp
template< class F, class... Args >
std::invoke_result_t<F, Args...>
    invoke( F&& f, Args&&... args ) noexcept(/* see below */);
```

In most other cases (including the entire `<algorithm>`), however, callables are passed by value:

```cpp
template< class T, class Compare >
const T& max( const T& a, const T& b, Compare comp );
```

So which way is it?

My current guideline is:

- If `g` is mainly about the act of calling `f`, then by reference;
- Otherwise (the default), by value.

This simplifies the code a bit, and make the callable directly usable with `<algorithm>` functions.

For callables that are not copyable, or when copying is undesirable, one can use `std::ref` to wrap them:

```cpp
struct UniqueFunctor {
    UniqueFunctor(UniqueFunctor const&) = delete;

    void operator()(/* ... */) const { /* ... */ }
} uniq_f;

// Error: UniqueFunctor is not copyable
std::ranges::for_each(rg, uniq_f);

// Ok: std::reference_wrapper is copyable
std::ranges::for_each(rg, std::ref(uniq_f));
```

Unless your callable does something different in its &&-qualified call operator:

```cpp
struct MysteriousFunctor {
    void operator()(/* ... */) const { /* Do one thing */     }
    void operator()(/* ... */)    && { /* Do another thing */ }
} mf;

std::invoke(mf, /* ... */ );             // Do one thing
std::invoke(std::move(mf), /* ... */ );  // Do the other thing
```

passing by value + `std::ref` is sufficient.


## Concepts, `if constexpr`, and `static_assert` Are Your Friends

These three just make writing templates much more enjoyable (or, should I say, *much less painful*). I'll probably write another post talking about them in the future. Briefly speaking,

- Concepts is just better than `enable_if` in every way
- `constexpr if` is more readable than (partial) specialization + overload + tag dispatch
  - `static_assert(false)` in C++23 makes the `else` branch cleaner
- `static_assert` helps locate problems earlier, and can help "debugging" templates

For now, the recommendation is:

- Get a C++20 (or better yet, C++23) enabled compiler
- Start building your projects in `-std=c++20`/`-std=c++23`
- At the very least, adopt C++17

If you or your team are working with templates, the recent C++ editions have a lot to offer.
