---
title: "Improve Diagnostics with std <stacktrace>"
date: 2025-05-13
---


## Intro

One thing that makes Python debug-friendly is: when an exception is raised, the stacktrace is printed. Consider:

```py
def g(n):
    if n % 2 == 1:
        raise Exception("That's odd")

def f(n):
    g(n)
    g(n + 1)

def main():
    n = int(input("Enter a number:"))
    f(n)

main()
```

When the above program is executed:

```
$ python main.py 
Enter a number:42
Traceback (most recent call last):
  File "main.py", line 13, in <module>
    main()
  File "main.py", line 11, in main
    f(n)
  File "main.py", line 7, in f
    g(n + 1)
  File "main.py", line 3, in g
    raise Exception("That's odd")
Exception: That's odd
```

Now, rewriting the program in C++:

```cpp
#include <iostream>
#include <stdexcept>


void g(int n) {
    if (n % 2 == 1) {
        throw std::runtime_error("That's odd");
    }
}

void f(int n) {
    g(n);
    g(n + 1);
}

int main() {
    int n;
    std::cout << "Enter a number:";
    std::cin >> n;
    f(n);
}
```

When compiled and run with the same input:

```
$ ./a.exe 
Enter a number:42
terminate called after throwing an instance of 'std::runtime_error'
  what():  That's odd
```

As we can see, only the explanatory string is printed. There is no stacktrace.

- In fact, even this behavior - printing the explanatory string of `std::runtime_error` on termination - is not guaranteed by the standard; all implementations do this because it is incredibly helpful to debugging

One would need to fire up a debugger such as `gdb` and run the program under it in order to obtain the stacktrace at the point of termination.

C++23 introduced [`<stacktrace>`](https://en.cppreference.com/w/cpp/header/stacktrace), which is based on `boost::stacktrace`. Can we now enrich the exception message with stacktrace like Python, using only the standard library? Let's give it a try.



## std::stacktrace

There are two main operations we need when it comes to stacktrace.

1. Get the stacktrace, which is `std::stacktrace::current()`
2. Print the stacktrace, which is `std::cerr << st`
    - Alternatively, convert to a human-readable string first: `std::to_string(st)`

A slight modification to the above program:

```cpp
#include <iostream>
#include <stacktrace>
#include <stdexcept>


void g(int n) {
    if (n % 2 == 1) {
        throw std::runtime_error("That's odd\n" + std::to_string(std::stacktrace::current()));
    }
}

void f(int n) {
    g(n);
    g(n + 1);
}

int main() {
    int n;
    std::cout << "Enter a number:";
    std::cin >> n;
    f(n);
}
```

Then compile and run as before.

- With GCC 15 and earlier, it needs to compile with `-lstdc++exp`

The program output is now:

```
terminate called after throwing an instance of 'std::runtime_error'
  what():  That's odd
   0# g(int) at /app/example.cpp:8
   1# f(int) at /app/example.cpp:14
   2# main at /app/example.cpp:21
   3#      at :0
   4# __libc_start_main at :0
   5# _start at :0
   6# 
```

This is already much nicer than before!



## stack_runtime_error

There are a few improvements to make. First of all, we don't want to write the lengthy

```cpp
throw std::runtime_error("That's odd\n" + std::to_string(std::stacktrace::current()));
```

every time. Instead, it would be nice to have a custom exception class that does the above for us:

```cpp
throw stack_runtime_error("That's odd");
```

How to write such a `stack_runtime_error` class?

My first attempt was:

```cpp
class stack_runtime_error : public std::runtime_error {
  public:
    stack_runtime_error(const std::string& what_arg) 
    : std::runtime_error(what_arg + "\n" + std::to_string(std::stacktrace::current()))
    {}
};
```

It sort of works, except that it prints one extra useless frame:

```
stderr
terminate called after throwing an instance of 'stack_runtime_error'
  what():  That's odd
   0# stack_runtime_error::stack_runtime_error(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) at /app/example.cpp:8
   1# g(int) at /app/example.cpp:15
   2# f(int) at /app/example.cpp:21
   3# main at /app/example.cpp:28
   4#      at :0
   5# __libc_start_main at :0
   6# _start at :0
   7# 
```

To remove this visual noise, I added a default argument to the constructor that evaluates to `std::stacktrace::current()`:

```cpp
class stack_runtime_error : public std::runtime_error {
  public:
    stack_runtime_error(const std::string& what_arg, const std::stacktrace& st = std::stacktrace::current()) 
    : std::runtime_error(what_arg + "\n" + std::to_string(st))
    {}
};
```

And now it only prints the frames we're interested in.

We can further make it cleaner and slightly more efficient by using `std::format` instead of string concatenation:

```cpp
class stack_runtime_error : public std::runtime_error {
  public:
    stack_runtime_error(const std::string& what_arg, std::stacktrace st = std::stacktrace::current()) 
    : std::runtime_error(std::format("{}\n{}", what_arg, st))
    {}
};
```

This little utility class has helped me several times when debugging and/or producing meaningful diagnostic messages to users.



## dynamic_assert

Another thing that makes Python convenient to debug is the `assert` statement:

```py
def div(a, b):
    assert b != 0, "Divisor is 0"
```

Especially with f-strings, it makes identifying the offending arguments incredibly convenient:

```py
def f(x):
    assert x >= 0, f"Negative input: {x}"
```

The assertion fire, of course, comes with nice stacktrace as well.

C++ inherits `assert` from C, when doesn't support custom message:

```cpp
void f(int x) {
    assert(x >= 0);
}
```

A common trick is to add the message with `&&`:

```cpp
void f(int x) {
    assert(x >= 0 && "Negative input");
}
```

However, the message must be a string literal; we cannot include the runtime value of `x` in the error message output.

This is the reason why every codebase has its own `ASSERT` macro to include more context in the error message.

We don't have f-strings in C++ (yet), but we do have have `<format>` and `<stacktrace>`. So I tried to create a `dynamic_assert` that can be used as:

```cpp
void f(int x) {
    dynamic_assert(x >= 0, "Negative input: {}", x);
}
```

The implementation is basically forwarding the arguments to `std::format`:

```cpp
template<class... Args>
void dynamic_assert(bool cond, std::format_string<Args...> fmt, Args&&... args) {
    if (!cond) {
        throw stack_runtime_error(std::format(fmt, std::forward<Args>(args)...));
    }
}
```

This works, except that old issue resurfaces - one extra useless frame is printed, creating visual noise:

```
 what():  Negative input: -1
   0# void dynamic_assert<int&>(bool, std::basic_format_string<char, std::type_identity<int&>::type>, int&) at /app/example.cpp:16
   1# f(int) at /app/example.cpp:22
```

At first, I tried to solve it using the same trick - adding a default argument that evaluates to `std::stacktrace::current()`:

```cpp
template<class... Args>
void dynamic_assert(bool cond, std::format_string<Args...> fmt, Args&&... args, const std::stacktrace& st = std::stacktrace::current()) {
    if (!cond) {
        throw stack_runtime_error(std::format(fmt, std::forward<Args>(args)...), st);
    }
}
```

Unfortunately, it doesn't work:

```
<source>: In function 'void f(int)':
<source>:22:19: error: no matching function for call to 'dynamic_assert(bool, const char [19], int&)'
   22 |     dynamic_assert(x >= 0, "Negative input: {}", x);
      |     ~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
<source>:22:19: note: there is 1 candidate
<source>:14:6: note: candidate 1: 'template<class ... Args> void dynamic_assert(bool, std::format_string<_Args ...>, Args&& ..., const std::stacktrace&)'
   14 | void dynamic_assert(bool cond, std::format_string<Args...> fmt, Args&&... args, const std::stacktrace& st = std::stacktrace::current()) {
      |      ^~~~~~~~~~~~~~
<source>:14:6: note: template argument deduction/substitution failed:
<source>:22:50: note:   cannot convert 'x' (type 'int') to type 'const std::stacktrace&' {aka 'const std::basic_stacktrace<std::allocator<std::stacktrace_entry> >&'}
   22 |     dynamic_assert(x >= 0, "Negative input: {}", x);
      |                                                  ^
```

In short, the argument `x` we supply is treated as `st` instead of `args`.

My workaround is to wrap `dynamic_assert` with a macro that adds the additional argument `std::stacktrace::current()` explicitly:

```cpp
#define dynamic_assert(...) dynamic_assert_impl(std::stacktrace::current(), __VA_ARGS__)

template<class... Args>
void dynamic_assert_impl(const std::stacktrace& st, bool cond, std::format_string<Args...> fmt, Args&&... args) {
    if (!cond) {
        throw stack_runtime_error(std::format(fmt, std::forward<Args>(args)...), st);
    }
}
```

This works, and now the program produces nice diagnostic messages.

- You can see the full example [here](https://godbolt.org/z/Wo74xTs4r) on compiler explorer.

By the way, I do not know a way to do this without macros. 

- If you have some ideas, feel free to let me know via [Github](https://github.com/biowpn/bioweapon/issues) or [Reddit](https://www.reddit.com/user/biowpn/)!



## Summary

In this post, I shared some debugging utilities that have helped me tremendously. While there's still a long way to go, recent versions of C++ provided tools to enhance debugging experience. Just as most new C++ features, they are not enabled by default; you need to write some user code to make the best use of them, as I did with `stack_runtime_error` and `dynamic_assert`. I'm eyeing `<debugging>` next, which provides `std::breakpoint`, another incredibly useful debugging facility. Stay tuned!
