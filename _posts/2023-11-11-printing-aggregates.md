---
title: "Printing Aggregates"
date: 2023-11-11
---

## Intro

Quoting Barry Revzin in [P2286 - Formatting Ranges](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2286r8.html):

> Printing is a fairly fundamental and universal mechanism to see what’s going on in your program.

Thanks to the paper, printing ranges such as `std::vector<T>` is now a solved problem.

There is another class of problems out there, though, which is printing structs:

```cpp
struct LogInEvent {
    long timestamp;
    int uid;
    int source;
};

...

void handler(const Event& evt) {
    std::println("Receive LogInEvent: timestamp={} uid={} source={}", evt.timestamp, evt.uid, evt.source);
}
```

Everyone has seen or written code like the above. And ... it can improved.


## Issues with "Inline Logging"

**1. Reduces code readability.** In reality, there are usually many more fields to print. As a result, the struct printing statement spans multiple lines. Such lengthy printing statements distracts us from reading the actual business logic code, especially so when interleaved with it.

**2. Leads to code duplication.** You have to duplicate everything if you want to print in another place. Now, imagine a new field is added and you want to print that as well - too bad! You have to revisit each and every print statement.

**3. Does not compose.** If `LogInEvent` is a sub-object of another object which you want to print, you'd have to duplicate the code again, even multiple times in a statement.

The solution is:


## Factor out the printing code

There are various ways to do it. You can use named functions, overload `operator<<`, or specialize `std::formatter`:

```cpp
// C era
void print(const LogInEvent *);

// C++98 era
std::ostream& operator<<(std::ostream&, const LogInEvent&);

// C++20 era
template <>
struct std::formatter<LogInEvent> {
    ...
};
```

All of them reduce code duplication; the C++ solutions even compose nicely.

The only problem is ... you still have to implement them, which is fairly mechanical:

```cpp
std::ostream& operator<<(std::ostream& os, const LogInEvent& x) {
    return os << "LogInEvent"
    << "("
        << "timestamp" << "=" << x.timestamp
        << ", " << "uid" << "=" << x.uid
        << ", " << "source" << "=" << x.source
    << ")";
}
```

thus,

**4. Specialized printing does not scale.** If you have `n` events, each with `m` fields, then the code size is `O(n * m)`. It quickly becomes tedious to maintain.


## Code generation

Some teams tackle this problem by using *code generation*. They define the structs in an external schema file, usually in an easy-to-parse format like `csv`, `json` or `yaml`, or some interface definition language (IDL). Then, a program reads these schema files and generates C++ codes. This is definitely a sound engineering strategy, and I've used it in production.

There are some minor issues with code generation, though.

* First, sometimes the structs are out of your control; for example, dependencies and third party libraries. You need to either manually add them to your schema file - which brings maintainence burden (ensure scheme file matches the actual definitions); or, enhance the code generator to parse C and/or C++ codes - which is complicating.
* Second, it complicates your workflow as it requires extra external tooling. It's fine if the code generator is a Python script; but it could also be some 3rd party library/program which you'll need to introduce dependency for and/or build that first.

Alas, *I just want to print a god damn struct!* I don't want to introduce code generation to my project just because!

Well, let's explore some alternatives.


## Generic Printing

If we want to implement a generic struct printing facility:

```cpp
// Works for any struct T
template <class T>
std::ostream& operator<<(std::ostream&, const T&);

...

/* Application */

struct Point {
    int x, y, z;
};

Point p{1, 2, 3};
std::cout << p; // prints "Point(x=1, y=2, z=3)"
```

There are two problems to solve:
1. Getting the name of the type `T`
2. Iterate through each field of `T`


### Getting the name of a template type

Problem 1 is more or less "solved", by leveraging `__PRETTY_FUNCTION__`, or the standardized [`std::source_location::function_name`](https://en.cppreference.com/w/cpp/utility/source_location/function_name):

```cpp
template <class T>
void show_me() {
    // std::cout << __PRETTY_FUNCTION__ << '\n';
    std::cout << std::source_location::current().function_name() << '\n';
}

...

show_me<LogInEvent>();
```

With GCC, the above program prints

```
void show_me() [with T = LogInEvent]
```

So the type name of `T` is embedded in the output, which can be extracted.

The output of `std::source_location::function_name` is implementation-defined, so this trick is not fully portable. But all three major implementations include type name in their output, and it's better to have something than nothing.


### Iterate through fields of a struct

Problem 2 is *a lot* harder.

But it can be done, at least for cases where the struct is an aggregate. Antony Polukhin wrote an outstanding library, [PFR](https://github.com/boostorg/pfr), that provides such feature out-of-the-box:

```cpp
Point p {1, 2, 3};
boost::pfr::for_each_field(p, [](auto& field_value){
    std::cout << field_value << " ";
});
```

The above program prints `1 2 3 `. Note that there's no macro, no manual binding, no generated code elsewhere for `Point`!

We'll briefly discuss how `for_each_field` is forged. The juice of the meat is converting an aggregate to a `std::tuple` of references:

```cpp
template <class T>
auto tie_as_tuple(T& x);

...

Point p {1, 2, 3};
auto t = tie_as_tuple(p);
```

`t` is `std::tuple<int&, int&, int&>`. With it, we can access the fields of `p` as:

```cpp
std::get<0>(t); // same as p.x
std::get<1>(t); // same as p.y
std::get<2>(t); // same as p.z
```

and iterate through them using index.

So how do we implement `tie_as_tuple`?

With C++17 and later, this is done by structured binding, enumerating the number of fields case by case:

```cpp
template <class T>
auto tie_as_tuple(T& x) {
    constexpr std::size_t n_fields = field_count<T>();

    if constexpr (n_fields == 0) {
        return std::tie();
    } else if constexpr (n_fields == 1) {
        auto [_1] = x;
        return std::tie(_1);
    } else if constexpr (n_fields == 2) {
        auto [_1, _2] = x;
        return std::tie(_1, _2);
    } else if constexpr (n_fields == 3) {
        auto [_1, _2, _3] = x;
        return std::tie(_1, _2, _3);
    }
    // ... as many branches as you like ...
    else {
        // Too many fields!
    }
}
```

So it's not fully generic. [PFR does it here](https://github.com/boostorg/pfr/blob/develop/include/boost/pfr/detail/core17_generated.hpp), supporting up to 100 fields by default, which should be enough in practice.

`field_count`, on the other hand, is more generic. The idea is: aggregate types can be aggregate-initialized with any number of arguments, up to the number of fields:

```cpp
struct Point {
    int x, y, z;
};

Point x{};           // OK
Point x{1};          // OK
Point x{1, 2};       // OK
Point x{1, 2, 3};    // OK
Point x{1, 2, 3, 4}; // Not OK, `Point` has 3 fields
```

So `field_count` can be implemented as (taken from [here](https://stackoverflow.com/a/72278502)):

```cpp
struct any_type { template <class T> operator T() {} };

template <class T>
    requires(std::is_aggregate_v<T>)
consteval std::size_t field_count(auto ...args) {
    if constexpr (!requires { T{ args... }; })
        return sizeof...(args) - 1;
    else
        return field_count<T>(args..., any_type{});
}
```

There's hope on the horizon, though. This paper, [P1061 - Structured Bindings can introduce a Pack](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p1061r5.html), proposes the following to work:

```cpp
auto [...xs] = p;
```

with P1061, `tie_as_tuple` can be:

```cpp
// works with any number of fields!
template <class T>
auto tie_as_tuple(T& x) {
    auto& [...xs] = x;
    return std::tie(xs...);
}
```

I sincerely hope that **P1061** will make it to C++ 26. It'll be a godsend for generic programming!


### Getting the field names of a struct

The latest PFR allows us to do one more cool thing:

```cpp
struct Point {
    int x, y, z;
};

boost::pfr::get_name<0, Point>(); // "x"
boost::pfr::get_name<1, Point>(); // "y"
boost::pfr::get_name<2, Point>(); // "z"
```

I was mind-blown when I first saw this.

Under the hood, it's done by leveraging `__PRETTY_FUNCTION__` in a clever way.

C++ allows pointers of global variables as non-type template parameters:

```cpp
template <auto V>
void show_me() {
    std::cout << __PRETTY_FUNCTION__ << '\n';
}

struct Point {
    int x, y, z;
};

Point g_point;

int main() {
    show_me<&g_point>();
}
```

With GCC 13.1, the above program prints:

```
void show_me() [with auto p = (& g_point)]
```

It works for sub-objects too:

```cpp
show_me<&g_point.x>();
show_me<&g_point.y>();
show_me<&g_point.z>();
```

output:

```
void show_me() [with auto p = (& g_point.Point::x)]
void show_me() [with auto p = (& g_point.Point::y)]
void show_me() [with auto p = (& g_point.Point::z)]
```

and finally, structured binding for indirection:

```cpp
auto& [_1, _2, _3] = g_point;
show_me<&_1>();
show_me<&_2>();
show_me<&_3>();
```

output is the same as the previous, from which the field names can be extracted.


## Putting everything together

```cpp
template <class T, typename = void>
consteval std::string_view type_name() {
    std::string_view s = std::source_location::current().function_name();
    auto i0 = s.find('T') + 4;
    auto i1 = s.find(';');
    return s.substr(i0, i1 - i0);
}

template <class T>
    requires(std::is_class_v<T> && std::is_aggregate_v<T>)
std::ostream& operator<<(std::ostream& os, const T& x) {
    os << type_name<T>();
    os << '(';
    boost::pfr::for_each_field(x, [&](const auto& field_val, auto field_idx) {
        if (field_idx > 0) {
            os << ", ";
        }
        os << boost::pfr::get_name<field_idx, T>() << '=' << field_val;
    });
    os << ')';
    return os;
}
```

Now we can finally print aggregates hassle-free:

```cpp
Point p{1, 2, 3};
std::cout << p << '\n';
// Point(x=1, y=2, z=3)

struct Line {
    Point a, b;
};
Line l{{1, 2, 3}, {4, 5, 6}};
std::cout << l << '\n';
// Line(a=Point(x=1, y=2, z=3), b=Point(x=4, y=5, z=6))
```

