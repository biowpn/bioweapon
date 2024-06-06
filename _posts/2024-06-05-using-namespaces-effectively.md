---
title: "Using namespaces effectively"
date: 2024-06-05
---


## Intro

When it comes to namespaces,
there is only one thing I'd like to get out right away:

- **Avoid `using namespace`**

Ok, that's too extreme and delibrately ambiguous.
More practical suggestions would be:

- **Keep `using namespace` as local as possible**
    - Only write `using namespace` in function scopes
    - Only write `using namespace` in .cpp files

A lot of the headaches can be avoided this way.



## Why avoid `using namespace`

If namespaces are designed to separate unrelated entities from each other, then `using namespace` defeats that purpose.
Introducing all names from one namespace to another mixes things up, worse if done so in a public header.
`using namespace` is akin to `from xxx import *` in Python, which is also considered a bad practice in that language.

Of course, as with almost everything in C++, there are exceptions.
One of the justified use of `using namespace` is `std::literals`, to make use of *literal suffix*:

```cpp
#include <chrono>
#include <string>
#include <string_view>

using namespace std::literals;

auto dur = 5us;               // dur is std::chrono::microseconds
auto str = "hello, world"s;   // str is std::string
auto svw = "hello, world"sv;  // svw is std::string_view  
```

But really, that's the only exception I can think of.

Still, typing long names is undesired.
Just think about `std::ranges::transform`, `std::filesystem::path`, and `std::chrono::system_clock`.
I totally agree, and that's why we have [Namespace aliases](https://en.cppreference.com/w/cpp/language/namespace_alias):

```cpp
namespace ranges = std::ranges;
namespace fs = std::filesystem;
namespace sc = std::chrono;
```

- Note: `using std::ranges;` doesn't work, nor does `using ranges = std::ranges;`. You have to do `namespace ranges = std::ranges;`

or even just using-declaration that introduces names one-at-a-time:

```cpp
using std::chrono::system_clock;
```

These options are considered better than plain `using namespace`
because we have *a lot more control* of what we import into our namespace.

- In the first case with namespace aliases, we're not really introducing anything;
we still need to write the namespace to get what's inside (e.g. `fs::path`), just shorter.
- In the latter case with using-declaration, we say *explicitly* what we want.

In comparison, `using namespace` is a wildcard; there's no control at the client/import site
of what actually gets in.



## Back to Basics

From [cppreference](https://en.cppreference.com/w/cpp/language/namespace):

> Namespaces provide a method for preventing name conflicts in large projects.

In languages without namespace such as C,
**prefix** is commonly used to prevent name conflicts,
usually in the form of `<library_name>_<function_name>`:

```c
// zlib
gzFile gzopen(const char *path, const char *mode);
int gzread(gzFile file, voidp buf, unsigned len);
int gzclose(gzFile file);
...

// libzip
zip_file_t *zip_fopen(zip_t *archive, const char *fname, zip_flags_t flags);
int zip_fclose(zip_file_t *file);
zip_int64_t zip_fread(zip_file_t *file, void *buf, zip_uint64_t nbytes);
...
```

If C had namespaces, those projects' API would probably look like:

```cpp
namespace gz {
    gzFile open(const char *path, const char *mode);
    int read(gzFile file, voidp buf, unsigned len);
    int close(gzFile file);
    ...
}

namespace zip {
    zip_file_t *fopen(zip_t *archive, const char *fname, zip_flags_t flags);
    int fclose(zip_file_t *file);
    zip_int64_t fread(zip_file_t *file, void *buf, zip_uint64_t nbytes);
    ...
}
```

So namespaces can be thought of a way to "extract" the common prefix, making long names easier to manage.
Whenever you have a bunch of functions/classes that share with same prefix/suffix,
consider creating a new namespace (or a class) for them, if they seem to be functionally related.

Over time, namespace has evolved to be more than just a space of names. It serves as **library boundary**.

Which leads to:



## The Namespaces & Interface Principle

Herb Sutter once said in [Namespaces & Interface Principle](http://gotw.ca/publications/mill08.htm):

> If you put a class into a namespace, be sure to put all helper functions and operators into the same namespace too

What happens if you don't?

Well, I didn't back when I was learning STL, and wrote some code as follows:

```cpp
#include <algorithm>
#include <string>
#include <vector>

namespace my {

struct Person {
    int id;
    std::string name;
};

} // namespace my

// Outside namespace my
bool operator<(const my::Person& lhs, const my::Person& rhs) {
    return lhs.id < rhs.id;
}

int main() {
    std::vector<my::Person> people;
    std::sort(people.begin(), people.end());
}
```

The above code does not compile, and generates more than a hundred lines of error messages,
quite daunting to the then-beginner me.

I remember digging through the errors and managed to find the most relevant line:

```
error: no match for 'operator<' (operand types are 'my::Person' and 'my::Person')
   69 |       { return *__it < __val; }
```

Oh compiler, but what do you mean? I clearly defined `operator<` for `my::Person`; it's **right there**!

To make it more confusing, plain `<` works just fine:

```cpp
int main() {
    my::Person a, b;
    ...
    if (a < b)  // Ok
}
```

After some trial and error, I found a fix - put `operator<` under the same namespace as where `Person` is defined:

```cpp
namespace my {

struct Person {
    int id;
    std::string name;
};

// Now in the same namespace as Person
bool operator<(const Person& lhs, const Person& rhs) {
    return lhs.id < rhs.id;
}

} // namespace my
```

And suddenly everything compiles and works.

- I didn't understand the *why* until this post was published and received [this reply](https://www.reddit.com/r/cpp/comments/1d9bzec/comment/l7e2j2w/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button) from reddit.

The compiler error message only suggests that the `operator<` defined in the global namespace isn't being considered a candidate at all. But why? Is it because `sort` is under `namespace std` so it doesn't look at the outer global namespace? That doesn't seem to be the case because this works:

```cpp
namespace your { // unrelated namespace
    bool less(const my::Person& lhs, const my::Person& rhs) {
        return lhs < rhs;
    }
}

int main() {
    my::Person a, b;
    your::less(a, b); // Ok
}
```

My takeway is: C++ is hard.

...

Ok, the real takeway is, Herb's advice again:

> If you put a class into a namespace, be sure to put all helper functions and operators into the same namespace too

But really, there are more compelling examples in Herb's post that lead to the same conclusion.
The above advice really saves you a lot of headache.



## Namespace and Project Structure

Namespaces can be nested, as can directories. I like to match namespace hierarchy with file system hierarchy for consistency.

Suppose the source file layout is:

```
.
└── include
    └── library_name
        ├── core.hpp
        ├── detail
        │   └── helper.hpp
        └── module_name_1
            └── core.hpp
```

Then `include/library_name/core.hpp` would be

```cpp
namespace library_name {
    ...
}
```

And `include/library_name/detail/helper.hpp` would be

```cpp
namespace library_name::detail {
    ...
}
```

And `include/library_name/module_name_1/core.hpp` would be

```cpp
namespace library_name::module_name_1 {
    ...
}
```

You get the idea: each layer of directory introduces a layer of namespace with the same name.

Other source files, including unittests, can be organized similarily:

```
src/
└── library_name
    ├── core.cpp
    ├── detail
    │   └── helper.hpp
    └── module_name_1
        └── core.cpp
```

* Private header `library_name/detail/helper.hpp` can be put under `src/` instead of `include/`,
to be separated from public headers.


# inline namespaces

Namespaces can provide multi-versioning *within* a library.
This is useful when multiple verions of the same entities need to co-exist in the same codebase.

For example:

```cpp
namespace gem {
    namespace v1 {
        struct Point {
            int x;
            int y;
        };
    }
    namespace v2 {
        struct Point {
            int y; // y goes first in v2
            int x;
        };
    }
}
```

Now, suppose we want to select one of the sub-namespace `v1` as the primary one,
so that `gem::XXX` means `gem::v1::XXX`. One way to do it is `using namespace`:

```cpp
namespace gem {
    namespace v1 {
        ...
    }
    namespace v2 {
        ...
    }
    using namespace v1; // pull everything under v1 out
}
```

The other way is to use `inline namespace`

```cpp
namespace gem {
    inline namespace v1 {
        ...
    }
    namespace v2 {
        ...
    }
}
```

In either way, `gem::Point` will refer to `gem::v1::Point`.

What's the difference? Before C++11, you cannot specialize a class template outside its namespace:

```cpp
namespace gem {
    namespace nested {
        template <class T> struct bar_traits;
    }
    using namespace nested;
}

template <>
struct gem::bar_traits<int> {}; // Error
```

With `inline namespace`, you can.

There may be some other arcane name-lookup difference,
but the recommendation stays: 

- **Prefer `inline namespace`** over `using namespace` for library multi-versioning.



# Afterword

From Zen of Python:

> Namespaces are one honking great idea -- let's do more of those!
