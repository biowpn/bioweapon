---
title: "type_sequence and Factory Method"
date: 2023-10-06
---

## Intro

Say we have a polymorphic class `Dinosaur`:

```cpp
struct Dinosaur {
    virtual ~Dinosaur() = default;
};
```

And some concrete classes:

```cpp
struct Diplodocus: Dinosaur {};

struct Stegosaurus : Dinosaur {};

struct Tyrannosaurus : Dinosaur {};
```

and a factory function that creates an instance based on some runtime value, say some string:

```cpp
// If `name` equals "Diplodocus", return a Diplodocus;
// Else if `name` equals "Stegosaurus", return a Stegosaurus;
// Else if `name` equals "Tyrannosaurus", return a Tyrannosaurus;
// Else, return nullptr.
std::unique_ptr<Dinosaur> make_dinosaur(std::string_view name);
```

This is a common pattern. Let's explore some ways to implement `make_dinosaur`.


## The I-Need-to-Go-Home-in-10-Minute Solution

The most straightfoward way is chaining `if`s:

```cpp
std::unique_ptr<Dinosaur> make_dinosaur(std::string_view name) {
    if (name == "Diplodocus") {
        return std::make_unique<Diplodocus>();
    } else if (name == "Stegosaurus") {
        return std::make_unique<Stegosaurus>();
    } else if (name == "Tyrannosaurus") {
        return std::make_unique<Tyrannosaurus>();
    } else {
        return nullptr;
    }
}
```

It's all good, and I doubt it'll take 5 minutes.

But, suddenly it starts to rain heavily and the elevator breaks. You have to unfortunately stay in the office for a bit longer.
You need to skill some time anyway, so you stare at the code, and start thinking:

**What can be done to improve the code?** What can you do now to make life easier for the future you?


## Analysis of the Solution

As we can see, there is high proportion of of boilerplate.
The only new information of each block of code is the dinosaur name. Everything else just repeats themselves:
- The variable `name`
- String comparison `==`
- `std::make_unique`

Any of the above can change as the requirements change in the future:
- The variable `name` is renamed
- Case-insensitive string comparison
- Returning `shared_ptr` instead
- Additional arguments to constructor

When any of the above happens, we would need to revise *every* block carefully.

Besides, the name of the type and type itself are not related, so there's room for copy-pasta error when new dinosaurs are added.


## Macros to the rescue ... ?

We may use a helper macro to reduce the boilerplate:

```cpp
#define MAKE_DINO_IMPL(T) if (name == #T) \
        return std::make_unique<T>()
```

and

```cpp
std::unique_ptr<Dinosaur> make_dinosaur(std::string_view name) {
    MAKE_DINO_IMPL(Diplodocus);
    MAKE_DINO_IMPL(Stegosaurus);
    MAKE_DINO_IMPL(Tyrannosaurus);
    return nullptr;
}
```

Problems of macros aside, this *does* improve the solution.
It factors out all the repeating parts and leaves only the core information (the types) in the function body.

Can we do better yet? Let's try templates! Whenever we want to replace macros, templates are our friends.


## Templates

If we try to convert the macro to a function template, we might have:

```cpp
template <class T>
std::string get_typename();

template <class T>
bool make_dino_impl(std::unique_ptr<Dinosaur>& out, std::string_view name) {
    if (name == get_typename<T>()) {
        out = std::make_unique<T>();
        return true;
    }
    return false;
}
```

`get_typename` can be implemented by, for example, leveraging `__PRETTY_FUNCTION__`. Alternatively, we may add a static function `type_name()` to each concrete class:

```cpp
struct Diplodocus: Dinosaur {
    static std::string type_name() { return "Dinosaur"; }
};
```

Bottom line is, `get_typename<T>` is a (mostly) solved problem.


## A notion of list of types

Now that we have our templated `make_dinosaur_impl`, we want to apply it to all the dinosaur types. It's a `for_each`, but on types instead of on values!

And finally, I'm ready to introduce the star of the day: `type_sequence`.


```cpp
template <class... T>
struct type_sequence{};
```

I first bumped into this construct in [Andrei Alexandrescu's CppCon talk](https://youtu.be/va9I2qivBOA), where he "urged" the committee to add the one-liner to the standard. This construct is handy, even outside heavy template meta-programming, as it turns parameter packs into types and values which we can pass around much more easily.

With it, we can define our list of types as follows:

```cpp
using dinasour_types = type_sequence<
    Diplodocus,
    Stegosaurus,
    Tyrannosaurus
>;
```

And apply the "for_each" equivalent as:

```cpp
template <class... Ts>
std::unique_ptr<Dinosaur> make_dinosaur_from(type_sequence<Ts...>, std::string_view name) {
    std::unique_ptr<Dinosaur> p;
    (make_dino_impl<Ts>(p, name) || ...); // for each type in Ts ...
    return p;
}

std::unique_ptr<Dinosaur> make_dinosaur(std::string_view name) {
    return make_dinosaur_from(dinasour_types{}, name);
}
```


## Are we making things better though?

You may wonder. Is it really worth the trouble? All the template codes seem unnecessarily complex!

The answer is: **it scales better**.

Adding a new type to the factory function is as simple as adding it to the `dinasour_types`:

```cpp
using dinasour_types = type_sequence<
    Diplodocus,
    Stegosaurus,
    Tyrannosaurus,
    Parasaurolophus // I'm new!
>;
```

That's it, a type (and a comma). This is even simpler than the macro solution.

Better yet, `make_dinosaur_from` and `make_dino_impl` can be generalized into `make_object`:

```cpp
/// Find a type `T` in `Ts` matching `name`, and return std::make_unique<T>().
template <class P, class... Ts>
P make_object(type_sequence<Ts...>, std::string_view name);
```

Using this generalized `make_object`, we can create not only `Dinosaur`, but also `Widget`, `Channel`, and so on.


## Bonus: Using a map

So far, we've been searching the types one by one.
Alternatively, we can use a map whose keys are the type names and values are the function pointers:


```cpp
template <class T>
std::unique_ptr<Dinosaur> make_unique_dino() {
    return std::make_unique<T>();
}

template <class... Ts>
std::unique_ptr<Dinosaur> make_dinosaur_from(type_sequence<Ts...>, std::string_view name) {
    static std::unordered_map dinosaur_map {
        std::pair {get_typename<Ts>(), &make_unique_dino<Ts>} ...
    };
    if (auto it = dinosaur_map.find(std::string(name)); it != dinosaur_map.end()) {
        auto fn = it->second;
        return fn();
    }
    return nullptr;
}

std::unique_ptr<Dinosaur> make_dinosaur(std::string_view name) {
    return make_dinosaur_from(dinasour_types{}, name);
}
```


## Afterword

We have essentially splitted a function into several smaller ones, each doing a specific job:

- `make_unique_dino`: actually making the object, knowing the type
- `make_dinosaur_from`: search for the concrete type

It definitely takes more effort to set things up initially, and the rain has stopped long ago!
