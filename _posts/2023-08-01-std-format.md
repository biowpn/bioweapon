---
title: "Experience with std::format"
date: 2023-08-01
---


## Intro

With the recent release of GCC 13 and Clang 17, `<format>` is finally avaiable to the most of us.
I'm using GCC and got some hand-on experience with it.

I've been a long time user of [fmtlib](https://github.com/fmtlib/fmt),
the library which the standard `<format>` is based on.
So it's natural for me to try to use `<format>` the way I use `<fmt/core.h>`;
can it be done by simply replacing `fmt::` with `std::`?

## Formatting User Type

Printing user types has been a common task. In the past it is done by overloading `operator<<`.
With **fmtlib**, it is done by providing a formatter class specialization.

The example I've been using (and which many codes were written based on) since 2019 is:

```cpp
// the type I want to format
struct Point {
    int x, y;
};


namespace fmt {

// formatter for it
template<>
struct formatter<Point> {
    template <class Context>
    auto parse(Context& ctx) { return ctx.begin(); }

    template <class Context>
    auto format(const Point& p, Context& ctx) {
        return format_to(ctx.out(), "Point({}, {})", p.x, p.y);
    }
};

}
```

Does it work if I simply change `namespace fmt` to `namespace std`?

Unfortunately, no. The first error I got is:

```
C:/msys64/ucrt64/include/c++/13.1.0/format:3595:47: error: call to non-'constexpr' function 'auto std::formatter<Point>::parse(Context&) [with Context = std::basic_format_parse_context<char>]' 3595 |               this->_M_pc.advance_to(__f.parse(this->_M_pc));
```

It seems quite clear - the compiler wants `parse` to be `constexpr`.
This makes sense, as it may want to do some compile-time validation.
No problem. So I modified my code to be:

```cpp
    template <class Context>
    constexpr auto parse(Context& ctx) { return ctx.begin(); }
//  ^^^^^^^^^    
```

and compile again.

It still doesn't work. And there are walls of error texts.

Thankfully, as C++ programmers we're already used to dealing with walls of error text. Here was my then throught process:

The direct error is:

```
C:/msys64/ucrt64/include/c++/13.1.0/format:3241:38: error: no matching function for call to 'std::basic_format_arg<std::basic_format_context<std::__format::_Sink_iter<char>, char> >::basic_format_arg(Point&)'
```

This doesn't tell me much. So I keep reading.

Somewhere down the line:

```
C:/msys64/ucrt64/include/c++/13.1.0/format:2996:9: note:   template argument deduction/substitution failed:
C:/msys64/ucrt64/include/c++/13.1.0/format:2996:9: note: constraints not satisfied
...
C:/msys64/ucrt64/include/c++/13.1.0/format:2201:20: note: the required expression '__cf.format(__t, __fc)' is invalid
 2201 |       { __cf.format(__t, __fc) } -> same_as<typename _Context::iterator>;

```

I assume `__cf` is my formatter, `__t` is a `Point`, and `__fc` is the formatter context.
But how is its return type not iterator though?

Keep reading, and I found

```
cc1plus.exe: note: set '-fconcepts-diagnostics-depth=' to at least 2 for more detail
```

So I compile again with `-fconcepts-diagnostics-depth=2`. This time it includes a new error:

```
C:/msys64/ucrt64/include/c++/13.1.0/format:2201:20: error: passing 'const std::formatter<Point>' as 'this' argument discards qualifiers [-fpermissive]
```

And that's why! The member function `format(...)` needs to be `const`. Now everything compiles.

```
namespace std {

template<>
struct formatter<Point> {
    template <class Context>
    constexpr auto parse(Context& ctx) { return ctx.begin(); }

    template <class Context>
    auto format(const Point& p, Context& ctx) const {
        return format_to(ctx.out(), "Point({}, {})", p.x, p.y);
    }
};

}
```

To be fair, after this I checked the fmtlib documentation again,
and the up-to-date user type formatter example does include `constexpr` and `const`.
I guess I'll just have to keep these in mind from now on.


## Format String Forwarding

While `std::format` is here, `std::print` is not. This means that we have to write

```cpp
std::cout << std::format("The answer is {}\n", 42);
```

for now.

Still, it is desirable to provide a poor man's `print` to make it convenient:

```cpp
template<class FmtString, class ...Args>
void print(FmtString fmt, Args&&... args) {
    std::cout << std::format(fmt, std::forward<Args>(args)...);
}

int main() {
    print("This answer is {}\n", 42);
}
```

Unfortunately, the above code doesn't compile. The error is:

```
formatter.cpp: In instantiation of 'void print(FmtString, Args&& ...) [with FmtString = const char*; Args = {int}]':
formatter.cpp:32:10:   required from here
formatter.cpp:26:29:   in 'constexpr' expansion of 'std::basic_format_string<char, int>(fmt)'
formatter.cpp:26:29: error: 'fmt' is not a constant expression
   26 |     std::cout << std::format(fmt, std::forward<Args>(args)...);
      |       
```

This one took a little bit of digging. It turns out `std::format` only supports compile-time format string,
as it is mandated to perform format string checking at compile time.

The way it does this is to have a format string type that depends on the arguments (`format_string<Args...>`),
so it can "remember" the argument types. The constructor of `format_string` is `consteval`
and it cannot read through an "outside" `const char*`, which our `print` essentially does, hence the error.

The make our `print` work:

```cpp
template<class ...Args>
void print(std::format_string<Args...> fmt, Args&&... args) {
    std::cout << std::format(fmt, std::forward<Args>(args)...);
}
```

## Afterword

It is great to have `std::format` available, even though it is quite restricted.

If any of the following is required:
- C++ 17 or before
- Dynamic format string
- `print`

Then only **fmtlib** can fufill the requirements.

For reasons above, I expect **fmtlib** to continue to be widely used in the near future.
