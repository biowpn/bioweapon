---
title: "What does f(x) mean in C++?"
date: 2024-11-12
---

## Intro

It is universally agreed that C++ is a complex language. One reason is that its syntax is highly overloaded, meaning that the same code could mean many different things.
Let's do a simple mental exercise by considering the following code:

```cpp
f(x)
```

How many possible meanings can there be?



### Macros

First, let get macros out of the box. If any of `f` and `x` is a macro, `f(x)` can expand to basically anything.
Hereafter, we presume that `f` and `x` are not macros.



### Function Call

`f(x)` can be a call to function named `f` with argument `x`. Duh.

```cpp
void f(int);

int x = 0;
f(x);  // call to f. Simple as that.
```

There are a few sub cases:

1. If `f` is a function template, then template argument deduction takes place, and implicit template instantiation follows. The compiler automatically generates a specialization (`f<int>`) for you.

2. If `f` is an overload set, then overload resolution takes place. The compiler picks the best candidate for you.

Note that 1 and 2 can both happen in a single call.



### Indirect Function Call

If `f` is a pointer to function, then `f(x)` is the same as `(*f)(x)` - indirect function call.
The indirection comes from "dereferencing" the pointer to function to get the actual function.

This "shortcut" syntax of indirect function call is inherited from C.


```cpp
void g(int);

auto f = &g;  // f has type void (*)(int)
auto f = g;   // same as above; function decay

int x = 0;
f(x);     // call g(x)
(*f)(x);  // call g(x)
```

This is a very powerful technique. It is one way to get polymorphism.

Similarly, when `f` is a reference to function (yes, functions can have references):

```cpp
void g(int);

auto& f = g;  // f has type void (&)(int)

int x = 0;
f(x);  // call g(x)
```



### Call Operator

If `f` is an object whose type provides `operator()`, then `f(x)` invokes that call operator. Such `f` is commonly referred to as a *function object* or simply *functor*:

```cpp
struct F {
    void operator()(int);  // (*)
};

F f;
int x = 0;
f(x);  // calls (*)
```

Lambdas, `std::function`, and the return type of `std::bind`/`std::bind_front`/`std::bind_back`, are all examples of such.

Just like free functions, this `operator()` can be overloaded and templated.

- *Generic lambdas* have templated `operator()`



### Surrogate Function

On the other hand, a type can define a conversion operator that converts the instance to a function pointer, thus making it "callable". That is:

```cpp
void g(int);

struct F {
    using Fun_Pointer = void(*)(int);
    operator Fun_Pointer() { return g; }
};

F f;
int x = 0;
f(x);  // g(x)
```

This is known as "Surrogate Function".

- A *capture-less lambda* is also a surrogate function, because it can convert to a function pointer



### Object Creation

If `f` is a type (or an alias to a type) and `x` is an object, then `f(x)` creates a new object by calling the constructor:

```cpp
struct f {
    f(int);
};

int x = 0;
f(x);  // (1)
```

`(1)` creates a temporary object and immediately destroys it.

Right?

**Wrong**.

The above code doesn't compile. Here's what the compiler says:

```
<source>:19:11: error: conflicting declaration 'main()::f x'
   19 |         f(x); // (1)
      |           ^
<source>:18:13: note: previous declaration as 'int x'
   18 |         int x = 0;
```

`(1)` is actually a *definition* - it defines a variable `x` of type `f`. It is the same as `f x;`. The parentheses are redundant, but permitted nonetheless.

```cpp
struct f {};

f(x);  // Ok: declare a variable `x` of type `f`, then default-initialize it
```

The parentheses are even allowed in function parameter list, which I don't even want to talk about:

```cpp
void fun( int (x), int (y) );  // Why would anyone write it this way?
```

Anyway, to make `f(x)` do what we thought it'd do, we can wrap it with a pair of parentheses to make it an expression:

```cpp
struct f {
    f(int);
};

int x = 0;
(f(x));  // Create a temporary object and immediately destroys it
```

What about *just* `f(x);`? Can this create an (temporary) object?

~~Yes, if `f` is a class template, and class template argument deduction (CTAD) kicks in:~~

```cpp
template <class T>
struct f {
    f(T);
};

int main() {
    int x = 0;
    f(x);  // (?)
}
```

**Update**: Since this post was published, r/sagittarius_ack [pointed it out](https://www.reddit.com/r/cpp/comments/1gpshyq/comment/lwykjlw) that the above code [doesn't compile](https://godbolt.org/z/oW6rW1fd4), at least on GCC, Clang and MSVC, due to conflicting definition. [It does compile on EDG](https://godbolt.org/z/nx566Kcx5) though - EDG treats the line `(?)` as an expression statement that creates a temporary object and immediately destroys it.

Even more interestingly, consider the following code:

```cpp
#include <iostream>

template <class T = int>
struct f {
    f()  { std::cout << "1"; }
    f(T) { std::cout << "2"; }
    ~f() { std::cout << "3"; }
};

int x = 0;

int main() {
    f(x);  // (?)
}
```

There is implementation divergence:

- [GCC 14.2 accepts it](https://godbolt.org/z/6dM6j7fKM), and prints `13`
  - This suggests that GCC treats line `(?)` as a (shadowing) declaration, same as `f x;`
- [Clang 19.1.0 rejects it](https://godbolt.org/z/x9d65MxGx)
- [MSVC v19.40 rejects it](https://godbolt.org/z/sqrdhzP5v)
- [EDG 6.6 accepts it](https://godbolt.org/z/qWEo7Eh7x), but prints different output `23`
  - This suggests that EDG treats line `(?)` as an expression, same as `(f(x));`

I'm not sure what's going on.



### Invoke Static Call Operator?

Since C++23, call operators are allowed to be `static`:

```cpp
struct f {
    static void operator()(int);  // Ok since C++23
};
```

So, does this make `f(x)` an invocation to that static method, given `x` is an `int`?

**No**.

The correct way to call that static method is the same as how you usually call a static method - use the `::` scope resolution:

```cpp
f::operator()(x);  // Ok since C++23
```

By the way, C++ has always allowed you to invoke a static method through an instance (even it is not used at all):

```cpp
f v;
v(x);  // Ok
```

In short, despite its look, `f(x)` is *never* a call to the static call operator of type `f`.

- Similarly, despite `operator[]` is allowed `static`, you cannot call it like `T[arg]`. You have to write `T::operator[](arg)`.



### Constructor Declaration

If `f` is a class type and `x` is a type or type alias, then within the definition of `class f`, `f(x)` declares a constructor:

```cpp
struct x {};
class f {
    f(x);  // single-argument constructor
};
```



### Function Type

If both `f` and `x` are types or type aliases, then `f(x)` is a function type - functions that take an `x` and return an `f`.

```cpp
struct f {};
struct x {};

f(x);  // (1)
```

The above code compiles, so `(1)` must have formed a function type.

Right?

**Wrong**.

C++ does not allow "empty declarations" - declaration does not declare anything. In short, this is illegal:

```cpp
int;  // error:  declaration does not declare anything
```

But `f(x);` compiles. Are function types an exception to the rule?

No. Here, `f(x);` defines a new variable called `x`, same as before. **Even though `x` is already a type** prior to the line.

**Yes, you read it right**. For whatever reasons, C++ allows (re)using class names as variable names:

```cpp
class Bar {};   // Bar is a class
Bar b;          // Ok, define a variable `b`

int Bar = 0;    // Bar is an object now!
Bar b2;         // Error

// If you really want the class Bar back:
class Bar b2;   // Ok, define a variable `b2`
```

`f(x)` is interpreted as a function type elsewhere, when a type is expected:

```cpp
using F = f(x);          // Ok: declare a type alias F that refers to the function type
std::function<f(x)> ff;  // Ok
```

Just not `f(x);`.



## What we have So Far

We can produce a summary so far:

1. If `f` is a function, function template, or pointer to function, then `f(x)` is a function call

2. If `f` is an object, then `f(x)` either invokes the call operator (static or not), or invokes the conversion operator that returns a pointer to function and calls *that* instead

3. If `f` is a class type, then `f(x);` declares an object `x` of type `f`, *regardless of what `x` is*

4. If `f` is a class type and `x` is an object, and `f(x)` is part of an expression, then `f(x)` creates a temporary object via calling the constructor

5. If `f` is a class type and `x` is a class type, then `f(x)` is a function type

6. If `f` is a class template and `x` is an object, then `f(x)` creates a temporary object, provided that class template argument deduction succeeds; this is even the case for `f(x);`

Could there be more?



### Argument Dependent Lookup (ADL)

This is a special case of calling a regular function. Basically:

```cpp
namespace my {
    struct Bar {};

    void f(Bar) {}
}

int main() {
    my::Bar x;
    f(x);  // Ok; same as my::f(x)
}
```

Note that the `f` is unqualified, yet the compiler finds `my::f` because the argument `x`, a `my::Bar`, brings in everything under the namespace `my`.

What this means is, `f(x)` can mean `ns1::f(x)`, `ns1::ns2::f(x)`, and so on.



### Conversion Operator

When `f` is a type (or a type alias), and `x` has a conversion operator to `f`, `f(x)` may call that conversion operator:

```cpp
struct f;

struct X {
    operator f();  // (2)
};

X x;
auto y = f(x);  // Calls (2); `y` is an `f`
```

Then again, as we've seen more than once already, the following is a declaration:

```cpp
f(x);  // Declare a variable `x` of type `f`, then default-initialize it
```



### Functional-style Cast

Generally, when `f` is a type (or a type alias), `f(x)` is the syntax for *function-style cast* - cast the expression `x` to target type `f`.

As you can see, it gets the name because it *looks like* a function call, but is *not* necessarily a function call. For example, the pointer cast that we all like to do:

```cpp
class T {
    ... 
} object;        // An object ...
T* x = &object;  // and a pointer to it

using f = const unsigned char*;
auto p = f(x);  // No function call happening; it's just a cast
// Now we can poke around the object's internals like a pro
```

C-style casts, of which function-style cast is one form, despite its perceived simplicity and elegancy, are generally advised *against* using (in fact, all explicit casts are recommended to keep to a minimum). There is actually [a lot of mechanics](https://en.cppreference.com/w/cpp/language/explicit_cast) going on behind the scenes - C-style casts can do a combination of `const_cast`, `static_cast`, `reinterpret_cast` - and is even [an active issue](https://cplusplus.github.io/CWG/issues/2878.html) and evolving as of this writing.

- I would only use C-style casts for fundamental types - `int`, `double`, and the like. Pointers are excluded.


## Take Away

Now comes the summary, take two. Roughly, `f(x)` can mean these different things:

1. Some sort of function call to `f`
2. A cast to type `f` (including calls to constructor and conversion operator)
3. A function type
4. A declaration of `x`

`4` is really surprising and should be avoided, `3` is related to `1`. That leaves us only 2 and 1.

**`f(x)` should mean a function call**. Period. The fact that it *sometimes* means a declaration is unfortunate; `4` should be avoided. `3` is really a "prototype" of `1`. Now, if C-style casts are replaced with C++ casts (`const_cast`, `static_cast`, `reinterpret_cast`), then `f(x)` can really, *really* mean a function call in the codebase.

~~Or maybe I missed something?~~
