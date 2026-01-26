---
title: "Mixing N-phase Initialization"
date: 2026-01-25
---


## Intro

For a lack of a better title, allow me to clarify some concepts within the scope of this article.

Some classes are **two-phase initialized**: they usually have an `init()` method which you need to explicitly call to initialize the instance:

```cpp
App1 app;
app.init(config);
```

For two-phase initialized classes, they have this "*empty*" or "*uninitialized*" state, in between when the instance is created and when `init()` is called.

Other classes are **one-phase initialized**: their constructors do all the initialization:

```cpp
App1 app(config);
```

There is no empty state for one-phase initialized classes. These classes are almost always not default constructible.

Note that, there are classes that are able to perform both one-phase and two-phase initialization, such as `std::unique_ptr` and `std::fstream`. I'd classify them as **two-phase initialized** since there is an empty state for those classes. One-phase initialized classes *must* initialize in one phase. An example is `std::lock_guard` - you must provide a mutex to its constructor.

Also note that, N-phase initialization is a separate concern from RAII. Both one-phase and two-phase initialization classes can be RAII (when their destructor releases the resource) or not RAII (when their destructor does not).



## The Problem

Now we can talk about the problem I ran into recently.

Consider two classes, `X` and `Y`:

- `X` is two-phase initialized *only*; it lacks constructors that do one-phase initialization
- `Y` is one-phase initialized
- `Y` depends on `X` and must be initialized after `X`

This is how you usually initialize a pair of `x` and `y` in function scope:

```cpp
X x;
auto r = x.init("path/to/file", 4096);
Y y(x.data(), x.size(), r);
```

This works, but what if I want to put `x` and `y` as members of a single class, `C`?

``` cpp
class C {
    X x;
    Y y;

  public:
    C(): y( /* What goes here? x isn't ready yet! */ ) {}
};
```

Well, couldn't we add a constructor that does one-phase initialization to `X`?

This solves only part of the issue. The initialization of `Y` depends on the return value of `x.init()` call.

Ok, how about adding a default constructor to `Y` to make it two-phase initialized?

Unfortunately, the business nature of `Y` mandates it *cannot* have an empty state. Not without breaking a lot of semantics at least.

Hereafter, let's assume we cannot modify `X` and `Y`. Can we make `C` work?



## Delayed Construction

The crux of the issue is that the constructor of `Y` runs *before* the `init` of `x`. So we need to delay the construction of `Y` so that it happens *after* `x.init()`.

One way is, instead of having `Y y`, we can have `std::unique_ptr<Y> y`:

```cpp
class C {
    X x;
    std::unique_ptr<Y> y;

  public:
    C(const Config& config) {
        const char* path;
        size_t size;
        // ... use config to fill `path` and `size` ...
        auto r = x.init(path, size);

        y = std::make_unique<Y>(x.data(), x.size(), r);  // Ok
    }
};
```

This works, but we're incurring heap allocation and indirection every time `y` is used. Can we deal without heap allocation?

We can use `std::optional`:

```cpp
class C {
    X x;
    std::optional<Y> y;

  public:
    C(const Config& config) {
        const char* path;
        size_t size;
        // ...
        auto r = x.init(path, size);

        y = Y(x.data(), x.size(), r);  // Ok
    }
};
```

This feels hacky - an `optional` that is always engaged so it's not really *optional* in the literal sense - but it works! There's no heap allocation or indirection. There is still that extra `bool` from `optional` and the `->` awkwardness every time `y` is used. Can we do away with those as well?

Now we're entering the non-standard (read: dangerous) territory. We don't want automatic construction, and we don't want indirection. That leaves us with one way - raw storage and manual construction (and destruction!):

```cpp
class C {
    X x;
    alignas(alignof(Y)) std::bytes[sizeof(Y)] y_storage;
    
    Y& y() {
        return *reinterpret_cast<Y*>(y_storage);
    }

  public:
    C(const Config& config) {
        const char* path;
        size_t size;
        // ...
        auto r = x.init(path, size);

        new (y_storage) Y(x.data(), x.size(), r);  // or std::construct_at
    }

    ~C() {
        y().~Y();  // or std::destroy_at
    }
};
```

There are a few issues with this solution. One, it's ugly. Two, it doesn't yet handle copying and moving. Three, as far as I know, there is UB here:

```cpp
    return *reinterpret_cast<Y*>(y_storage);
```

It has to do with pointer interconvertibility. In short, pointer to an object's storage is *not* interconvertible to a pointer to that object.

- I think [P1839](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1839r7.html) is trying to relax rules in this area a bit. That said, P1839 only permits read-only operations; we're likely using `y` in a mutable way, so even P1839 cannot save us.
- `std::start_lifetime_as` cannot help us here either, since `Y` is not a trivially copyable type.
- And no, neither does `std::launder` apply here.

The 'fix' is to save the pointer returned by placement new and to access Y only through that pointer:

```cpp
class C {
    X x;
    alignas(alignof(Y)) std::bytes[sizeof(Y)] y_storage;
    Y* py;

  public:
    C(const Config& config) {
        const char* path;
        size_t size;
        // ...
        auto r = x.init(path, size);

        py = new (y_storage) Y(x.data(), x.size(), r);  // <--
    }

    ~C() {
        py->~Y();
    }
};
```

But now this is *worse* that the `std::optional` approach in every way. And we haven't even mentioned exception safety: what if `Y`'s constructor/destructor throws?

As far as delayed construction goes, I think `std::optional` is the most sane solution.



## Separate Construction

What if I want *just* `y` as a member of `C`, not `std::optional` or any other wrapper?

Looking from another angle, I could see the problem as a limitation of the member initialization list. They can only invoke constructors, and we cannot introduce temporary variables to save the result of `x.init()`.

```cpp
C (const Config& config):
    x(),                  // Can only call constructor, not `init()`
    y(x.data(), x.size(), /* Cannot use the result of `x.init()` */)
```

So what if we offload the initialization logic to a helper function that creates `x` and `y` for us, and we just take the result?

```cpp
std::pair<X, Y> create_xny(const Config& config) {
    const char* path;
    size_t size;
    // ... use config to fill `path` and `size` ...
    X x;
    auto r = x.init(path, size);
    Y y(x.data(), x.size(), r);

    return {std::move(x), std::move(y)};
}
```

Why, yes, we have delegating constructor:

```cpp
class C {
    X x;
    Y y;

  public:
    C(const Config& config): 
        C(create_xny(config))
    {}

    C(std::pair<X, Y> xny):
        x(std::move(xny.first)),
        y(std::move(xny.second))
    {}
};
```

This works, as long as `X` and `Y` are movable, and `X::data()` is stable:

```cpp
X x1;
void* ptr = x1.data();

X x2 = std::move(x1);
assert( x2.data() == ptr );  // key to correctness
```

I prefer the delegating constructor approach over delayed construction. I would generalize this further: **RAII members should preferably be initialized outside the containing class and moved in**. This separates concerns and makes the design more modular.

```cpp
using file_ptr = std::unique_ptr<FILE, 
                                 decltype([](FILE* fp) { if (fp) std::fclose(fp); })
                                >;

class Copy {
    file_ptr i_fp;
    file_ptr o_fp;

  public:
    // Bad: The logic for opening files is coded within the class.
    //      If the opening logic changes, we must modify the class itself.
    Copy(const char* i_path, const char* o_path):
        i_fp(std::fopen(i_path, "rb")),
        o_fp(std::fopen(o_path, "wb"))
    {}

    // Good: Initialization concerns are separated.
    //       The Copy class only handles the copying logic.
    Copy(file_ptr _i_fp, file_ptr _o_fp):
        i_fp(std::move(_i_fp)),
        o_fp(std::move(_o_fp))
    {}

    ...
};
```



## Immediate Lambda

After this post is published, I received a feedback from my coworker [Bernard](https://github.com/btzy) that I could use an immediate lambda to initialize `y` as such:

```cpp
class C {
    X x;
    Y y;

  public:
    C(const Config& config): 
        y(
            [&]{
                    const char* path;
                    size_t size;
                    // ... use config to fill `path` and `size` ...
                    auto r = x.init(path, size);
                    return Y(x.data(), x.size(), r);
            }()
        )
    {}
};
```

No helper function, no delegating constructor, just in-place construction! In fact, this doesn't even require `Y` being move-constructible, thanks to [copy elision](https://en.cppreference.com/w/cpp/language/copy_elision.html). Neat!



## Afterword

The class member initialization list is sometimes too restrictive to handle dependencies between one-phase and two-phase initialized members. Aside from delaying construction via wrappers, we can utilize helper functions and delegating constructors to "move-in" fully initialized instances. While this approach has clearer separation of concerns, it relies heavily on move semantics. An alternative is using immediate lambdas which does not rely on move semantics at all, at the slight cost of heavier constructor.
