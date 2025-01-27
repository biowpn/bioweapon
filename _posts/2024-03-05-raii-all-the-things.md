---
title: "std::unique_ptr as a Generic RAII Wrapper"
date: 2024-03-05
---

*The original title of this article was "RAII all the things?",*
*and the article didn't have enough focus. It's since been reworked*.

## Intro

`std::unique_ptr` is the prime example of RAII (Resource Acquisition Is Initialization).
It's *the* standard way of managing dynamically allocated memory:

```cpp
{
    // ptr is std::unique_ptr<int>
    auto ptr = std::make_unique<int>(42); // calls new
} // runs destructor, calls delete
```

In fact, `unique_ptr` can be customized to manage many more resources, not just memory.
For example, file handles, connection, opaque pointer to C context, and so on.

The trick is to provide a custom deleter:

```cpp
template<
    class T,
    class Deleter // Customization point
> class unique_ptr;
```

We will look at few examples.


### Smart File Pointer

Let's start with something simple:

```cpp
struct fcloser {
    void operator()(std::FILE* fp) const noexcept {
        std::fclose(fp);
    }
};

using file_ptr = std::unique_ptr<std::FILE, fcloser>;
```

Note that:

- The deleter is simply a callable, with pointer as the only argument
- There's no need to check for null-ness; `unique_ptr` does it for us
- The call operator is `noexcept`, since good destructors don't throw


### Memory-mapped File

Sometimes, the clean-up function is a bit more complex.

In POSIX, we have `mmap` that maps the process's virtual address space to some file ("allocation"),
and the corresponding deallocation function `munmap`:

```c
// https://man7.org/linux/man-pages/man2/mmap.2.html

// Create a new mapping
//   On success, returns a pointer to the mapped area
//   On failure, returns MAP_FAILED
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);

// Delete the mapping
int munmap(void* addr, size_t length);
```

Note that `munmap` takes a pointer and a length.
We can accommodate this by having the deleter remember the length:

```cpp
struct mem_unmapper {
    size_t length{};

    void operator()(void* addr) const noexcept {
        ::munmap(addr, length);
    }
};

using mapped_mem_ptr = std::unique_ptr<void, mem_unmapper>;
```

Note that we can have a `unique_ptr` to `void`! This is syntactically fine,
so long as we don't dereference (`*`) or member-access (`->`) it.

Semantically, we shouldn't interpret `unique_ptr<void, ...>` as "managing voidness", but rather:

- We have a pointer, `void*`, that refers to some resource (in this case, a region of memory)
- The pointer is used to clean up the resource referred to

As with all customized `unique_ptr`,
it is recommended to provide a factory function to fill the role of `make_unique`:

```cpp
[[nodiscard]] inline mapped_mem_ptr make_mapped_mem(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    void* p = ::mmap(addr, length, prot, flags, fd, offset);
    if (p == MAP_FAILED) { // MAP_FAILED is not NULL
        return nullptr;
    }
    return {p, mem_unmapper{length}}; // unique_ptr owns a deleter, which remembers the length
}
```


### File Descriptor

For some resource, there isn't even a pointer to begin with.
An example would be the UNIX file descriptor, which is just an `int`.
Can we still use `unique_ptr` for it?

The answer is **yes**. It turns out that `unique_ptr<T, D>::pointer` doesn't need to be an actual pointer;
it can be any type that satisfies [NullablePointer](https://en.cppreference.com/w/cpp/named_req/NullablePointer)!

So can it be `int`?

... Not quite.
A requirement of `NullablePointer` is that it must be able to construct from `nullptr`,
which `int` cannot:

```cpp
int i = nullptr;  // ill-formed, for good reasons
```

And even if it could, say initialized to 0 as `int i = NULL;` does,
we probably don't want this behavior: `0` is a valid file descriptor, and UNIX uses `-1` as the sentinel value for invalidness/null-ness.

Therefore, we must first roll up a wrapper type:

```cpp
// Minimally satisfy NullablePointer. Intentionally non-RAII
class file_descriptor {
    int fd_{-1};
public:
    file_descriptor(int fd = -1): fd_(fd) {}
    file_descriptor(nullptr_t) {}
    operator int() const { return fd_; }
    explicit operator bool() const { return fd_ != -1; }
    friend bool operator==(file_descriptor, file_descriptor) = default; // Since C++20
};
// All functions can be marked noexcept, constexpr
```

Then the deleter:

```cpp
struct fd_closer {
    using pointer = file_descriptor; // IMPORTANT
    void operator()(pointer fd) const noexcept {
        ::close(int(fd));
    }
};
```

The member typedef `pointer` is important.
When it is present, `unique_ptr` will use it as the `pointer` type;
otherwise, it falls back to `T*`.

So what we should use for `T`?

In this case, since we don't do `*` or `->` (the only places where `T` is used),
`T` can literally be anything:

```cpp
using unique_fd = std::unique_ptr<int,             fd_closer>; // Ok
using unique_fd = std::unique_ptr<file_descriptor, fd_closer>; // Ok
using unique_fd = std::unique_ptr<void,            fd_closer>; // Ok
```

... Even though they are all not quite right.
We are not managing `int` or `file_descriptor` or `void`, but rather some UNIX file hidden from us.

For readablity, it's worth making a opaque type:

```cpp
struct unix_file;
using unique_fd = std::unique_ptr<unix_file,       fd_closer>; // Ok, recommended
```

The takeaway is: we can think of `Deleter::pointer` as a handle that refers to some resource.

On a side note, there's a gotcha our `unique_fd`:

```cpp
unique_fd fd{STDIN_FILENO};
assert(int(fd.get()) == STDIN_FILENO); // Fail!
```

This is because `STDIN_FILENO` is a macro that expands to `0`,
and a literal zero is also `nullptr`.
Therefore, between the two viable constructor overloads:

```cpp
unique_ptr( std::nullptr_t ); // (1)
unique_ptr( pointer );        // (2)
```

`(1)` is picked because it's a direct match.
Our `file_descriptor`, when constructed from `nullptr`, takes the value `-1`.

The fix is:

```cpp
unique_fd fd{int(STDIN_FILENO)}; // Ok
unique_fd fd{file_descriptor(STDIN_FILENO)}; // Ok too
```

This is one of those "C++ being C++" moments and why I have a love/hate abusive relationship with it.
Anyway, as suggested above, providing a factory function for your RAII class minimizes pitfalls like this.


## unique_resource

If you feel like the above dancing around with `Deleter::pointer` is hacky,
you are not alone. There was proposal to introduce a true generic RAII wrapper,
[`unique_resource`](https://en.cppreference.com/w/cpp/experimental/unique_resource), into the standard.
It is now under the `std::experimental`, so your vendor may have provided it already.
There are also third-party implementations available.


## Afterword

While it has limitations, `std::unique_ptr` is a great template for us to implement custom RAII classes.
Doing so can enhance resource safety, simplify client code (encapsulation),
and unify the resource-managing API to increase consistency and reduce cognitive load.
