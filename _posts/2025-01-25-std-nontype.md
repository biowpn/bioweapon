---
title: "std::nontype_t: What is it, and Why?"
date: 2025-01-25
---

## Intro

The other day, I was looking at the new C++26 [`std::function_ref`](https://en.cppreference.com/w/cpp/utility/functional/function_ref) on cppreference.
One of constructors looks like this:

```cpp
template< auto f >
function_ref( std::nontype_t<f> ) noexcept;  // (3)
```

In fact, there are two other constructor which also require `std::nontype_t`.

What is this `std::nontype_t` for?

The name of this construct is amusing to me as well.
It says `nontype`, but it also says `_t`. So is it a type or not?



## The Official Answer

The [cppreference page about `std::nontype_t`]() says:

> 1) The class template std::nontype_t can be used in the constructor's parameter list to match the intended tag.

... that doesn't tell us much. The second point says:

> 2) The corresponding std::nontype instance of (1) is a disambiguation argument tag that can be passed to the constructors of std::function_ref to indicate that the contained object should be constructed with the value of the **non-type template parameter** V.

At least now we know how it got its name: `nontype` refers to Non-type Template Parameter. Which, in turn, is a bad name by itself; a better name could have been "Value Template Parameter".

Anyway, `std::nontype_t` seems to have something to do with tag dispatch - a common method to ~~beat~~ help the overload resolution machinery.

But which constructors of `std::function_ref` do we need to disambiguate? Why is there ambiguity in the first place?



## The Long Answer

If we look at the `std::function_ref` constructors again, we'll see the first one looks like:


```cpp
template< class F >
function_ref( F* f ) noexcept;  // (1)
```

This constructor accepts a function pointer, same as `(3)`. If we want `(3)`, we have to go through `std::nontype`:

```cpp
void f();

auto g1 = std::function_ref(f);                // Select (1)
auto g2 = std::function_ref(std::nontype(f));  // Select (3)
```

But then it begs the question: why do we need both `(1)` and `(3)`, since both of which mean to wrap function pointers?

Well, as a rule of thumb, if a value is known at compile time, then the compiler can make use of the information and potentially generate more efficient code. This is the whole premise of `constexpr` machinery 

- There are reasons other than efficiency, for example enable more functional generic code.

Thus, it can be argued that `(3)` exists for efficiency reason.

Note that `std::function_ref` also support member function pointers, but only if their value is known at compile time. This is what the 4th and the 5th constructors are for:

```cpp
template< auto f, class U >
function_ref( std::nontype_t<f>, U&& obj ) noexcept;        // (4)

template< auto f, class T >
function_ref( std::nontype_t<f>, /*cv*/ T* obj ) noexcept;  // (5)
```

There is no overload that accepts runtime member function pointers:

```cpp
template< class F, class U >
function_ref( F f, U&& obj ) noexcept;  // Not a thing
```

Why can't `function_ref` accept runtime member function pointers?

I think it's for efficiency consideration, in particular **space efficiency**. Member function pointers can be larger than function pointers. On x86-64 with Itanium ABI, they can be as big as 32 bytes, whereas function pointers are 8 bytes. Storing member function pointer will increase the size of `std::function_ref`, which is not ideal for people that do not require this feature, and goes against the "Don't Pay For What You Don't Use" golden rule of C++.

- Member function pointers can't be stored indirectly (on the heap) either, because `std::function_ref` is required to be trivially copyable.

Note that I skipped the other details of `std::function_ref`, things like `thunk_ptr` and `bound_entity`. They are not necessary for understanding `std::nontype_t`.

- On this topic, there is an [excellent article](https://www.foonathan.net/2017/01/function-ref-implementation/) exploring the implementation details and challenges of `function_view` (a then-name for `function_ref`), which I learn a lot from.

What I *do* like to expand further is:

**`std::nontype_t` is a library fix for a hole in the language.**



## A Deeper Problem In the Language

Our problem can be summarized as follows:

- We are passing a value to a function
- Sometimes the value is known at compile time, sometimes it's not
- The function wants to do different things depending on that

This would, for example, catch more errors at compile time, offering more safety. Consider:

```cpp
int get_2nd_value(std::array<int, 2> const& arr) {
    return arr[2];  // Oops!
}
```

The above function compiles today, but always invokes undefined behavior. Though, a good compiler will generate warning for it.

The `operator[]` of `std::array` is directly responsible. It looks like this:

```cpp
int& operator[](int idx) {
    // Even the value is known at compile-time at call-site,
    // within the function it has to be treated like a runtime value
    return data_[idx];
}
```

This problem is known as the **`constepxr` function parameter problem**. As of C++23, function parameters can never be constant expressions, even for `consteval` functions (I've briefly touched this topic in this [post](https://biowpn.github.io/bioweapon/2024/02/17/constexpr-consteval-function.html) before).

If we were to follow the spirits of `std::function_ref`, we would add an overload that looks like:

```cpp
template <auto I>
int& operator[](std::nontype_t<I>) {
    static_assert(0 <= I && I < N, "out-of-bound access");
    return data_[I];
}
```

and then users would write:

```cpp
int get_2nd_value(std::array<int, 2> const& arr) {
    return arr[std::nontype(2)];  // Caught the bug! static_assert fires
}
```

Of course, nobody would write code like this. **Nobody wants to write code like this**.
This problem cannot be solved without a language solution.

So, what has been done so far?



## The Proposal

David Stone wrote the paper, [P1045 - constexpr Function Parameters](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1045r1.html), back in 2019. The core idea is allowing functions to overload base on the `constexpr`-ness of the parameters:

```cpp
void f(int i);            // (1)

void f(constexpr int i);  // (2)
```

`(1)` and `(2)` are considered two different functions.

Of course, with `constexpr` parameters, the function would need to be turned into function templates. This is because of value-dependent types:

```cpp
auto make_char_array(constexpr int n) {
    return std::array<char, n>{};
}

make_char_array(1);  // std::array<char, 1>
make_char_array(2);  // std::array<char, 2>
```

In this example, `make_char_array` returns different types on different values of its parameter. Regular functions can't do that, only function templates can, but with template parameters instead of formal parameters: Today, `make_char_array` is written as

```cpp
template <int n>
auto make_char_array() {
    return std::array<char, n>{}; 
}
```

and invoked as

```cpp
make_char_array<1>();  // std::array<char, 1>
make_char_array<2>();  // std::array<char, 2>
```

So one way to think of `constexpr` function parameters is that they are syntactic sugars for writing function templates and invoking them without `<>`, which is nice on two counts.

So what's the issue? Why isn't it in the standard yet?

From what I can tell, there isn't fundamental reason that blocks the paper. It is likely that the committee just prioritized other papers, and/or the paper author was busy with other work.

Personally, I think the `constexpr` function parameter issue is important, more so than a few features that were added to C++20 / C++23.



### Maybe constexpr, Maybe not

On an interesting note, the paper even discussed about of perfect forwarding constexpr-ness. Without it, there would be explosion of overloads:

```cpp
// With N parameters, there are 2**N overloads:
void f(int i, int j);
void f(constexpr int i, int j);
void f(int i, constexpr int j);
void f(constexpr int i, constexpr int j);

// And what if you don't even know the number of parameters?
template <class ...Args>
void f(Args...);
```

The rvalue-lvalue perfect forwarding and `deducing this` went great length solving this problem, so it's expected that constexpr function parameters should have something similar in play. The paper proposed a `maybe_constexpr` specifier, and the accompany `is_constant_value` helper:

```cpp
void f(maybe_constexpr int i) {
    if constexpr (std::is_constant_value(i)) {
        static_assert(i > 0, "i must be positive");
    }
}
```

To me, `maybe_constexpr` function parameters looks a lot like `constexpr` functions. Why, `constexpr` functions are *precisely* **maybe** `constexpr`! Only the `consteval` functions are definitely `constexpr`. So, maybe, `maybe_constexpr` should just be `constexpr`, and `constexpr` parameters should just be `consteval` parameters.

But if we have all three (regular parameters, maybe-constexpr parameters, constexpr parameters), regardless of the syntax choice, we'd have:

```cpp
void f(int i);                  // (1)
void f(constexpr int i);        // (2)
void f(maybe_constexpr int i);  // (3)
```

Overload explosion aside, there is the issue of overload resolution. Consider:

```cpp
void g(maybe_constexpr auto i) {
    f(i);  // Which f is chosen?
}
```

Does `f(i)` always choose `(3)` since they have matching specifier? Or does it choose either `(1)` or `(2)` depending on whether `i` is `constexpr`, never `(3)`? What if `(1)` is removed, or `(2)`, or both - does `(3)` become viable now?

There is also the problem that the new keyword `maybe_constexpr` occupies the same space as where concepts are in today, so it risks breaking code.

To keep things simple, I think we should just have one specifier `constexpr` for function parameters, and have them mean "maybe constexpr". This way, we would't need to duplicate functions. We would slap `constexpr` specifiers to existing function's parameters like how we've been doing for functions. And since `constexpr` parameters turns functions into function templates, we may start with only allowing `constexpr` parameters for function templates.



## Summary

`std::nontype_t` is really a wrapper for compile time constants. The name `nontype` comes from non-type template parameter. The deeper problem is that function parameters cannot be used in `constexpr` context, and I don't expect this issue can be fixed soon.

Will more and more code start using `std::nontype_t` to signify a compile-time value is passed? There already exists many libraries with their own constant value wrappers. Will they migrate to `std::nontype_t`?

Only time will tell.

