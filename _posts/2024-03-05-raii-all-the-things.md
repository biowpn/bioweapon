---
title: "RAII all the things?"
date: 2024-03-05
---

## Intro

**RAII** stands for "Resource Acquisition Is Initialization".
It's ... probably one of the worst named term in C++,
at the same time, one of the most powerful programming idioms.
The naming is bad because it leaves out the argubly more important second half - 
Resource Release Is Finalization.

Put it simple: the destructor does some clean up.

```cpp
{
    std::ofstream ofs; // Constructor need not acquire resource
    if (/* some runtime condition */) {
        ofs.open("hello.txt");
        ofs << "Hello";
    }
    ...
} // Destructor runs:
  //   If file is open, close the file after flushing all the un-written data;
  //   Otherwise, do nothing
```

There are many more examples of RAII classes in the standard library:
all dynamically-sized containers, `unique_ptr`, `scope_lock` (`lock_guard`), and so on.


## Why RAII?

There are numerous benefits to RAII.
Resource safety is one (prevent resource leak),
program correctness is another (e.g., not releasing mutex causes deadlock).
RAII even composes nicely,
thanks to the member initialization/finalization order guaranteed by C++:

```cpp
// Assume A, B, C are all RAII classes.
struct A { A() /* Constructor may throw */; ~A() noexcept; };
struct B { B() /* Constructor may throw */; ~B() noexcept; };

struct C {
    A a;
    B b;
    // Initialization order is same as declaration order (a, b)
    // Finalization order is the exact inverse (b, a)

    C()
    // Construct `a`, then `b`;
    // if `b` throws, destroy `a`; C() is not run
    { // `a` and `b` now good
        ... // initialization, which may depend on `a` and `b`
            // if throw, destroy `b` then `a`; ~C() is not run
    }

    ~C() noexcept
    { // `a` and `b` still good
        ... // finalization
    }
    // Destroy `b` then `a`
};
```

And all of these good things are done automatically by the compiler for us!
This greatly reduces developer's cognitive load; we can just "write it and forget about it".

But, if RAII is so great, and has been available since C++98, a natural question to ask is:


## Why is RAII not everywhere?

Unfortunately, we still see manual resource management code,
or even worse - the lack of any such code, especially in legacy codebase.
Programmer's laziness/incompetence may be blamed, but a fact that should not be overlooked is:

Writing a good RAII class by hand is tedious.

- First off, destructor. Does the class have an empty state? Is the destructor `noexcept`?
- Next, you want to make the class move-only because the resource is expensive/impossible to copy,
  which means writing move ctor / move assign operator and deleting copy ctor / copy assign operator (rule of 5)
    - Did you mark the move functions `noexcept`?
- Then you need an accessor to get the resource
- Then you need some way to release the resource early, some `close`/`clear`. Maybe `swap` as well?
- And finally ... the constructors. What overloads should you provide? `explicit`/`noexcept`?
- Nobody expects this, but `constexpr`?

As we can see, aside from writing the code, there are tons of details to consider and choices to make.
When time-to-market is important, programmers may just give in.

Fortunately, there's a way out:


## std::unique_ptr to the rescue

`unique_ptr` already solved the same problems.
It can be customized to manage any resource, not just heap-allocated memory.
The trick is to provide a custom deleter:

```cpp
template<
    class T,
    class Deleter // Customization point
> class unique_ptr;
```

This way, we can eliminate much of the boilerplate when it comes to writing RAII classes. Here are a few examples.


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

- The deleter is simply a callable, with the pointer as argument
- There's no need to check for null-ness; `unique_ptr` does that for us
- Consider marking the call operator `noexcept` if possible


### Memory-mapped File

Sometimes, the deleter is a bit more involved.

In POSIX, we have `mmap` that maps the process's virtual address space to some file ("allocation"),
and the corresponding deallocation function `munmap`:

```c
// https://man7.org/linux/man-pages/man2/mmap.2.html

// Create a new mapping; on success, returns a pointer to the mapped area
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

Note that we can have a `unique_ptr` to `void`! This is fine,
so long as we don't dereference (`*`) or member-access (`->`) it.
We shouldn't read `unique_ptr<void, ...>` as "managing voidness", but rather:

- We have a pointer, `void*`, that refers to some resource (in this case, a region of memory);
- We clean up the resource referred to by the pointer

As with all customized `unique_ptr`,
it is highly recommended to provide a factory function as a substitute for `make_unique`:

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
The prime example would be the UNIX file fescriptor, which is just an `int`.
Can we still use `unique_ptr` for it?

The answer is yes. It turns out that `unique_ptr<T, D>::pointer` doesn't need to be an actual pointer;
it can be any type that satisfies [NullablePointer](https://en.cppreference.com/w/cpp/named_req/NullablePointer)!

So can it be `int`?

... Not quite.
A requirement of `NullablePointer` is that it must be able to construct from `nullptr`,
which `int` cannot:

```cpp
int i = nullptr;  // ill-formed, for good reasons
```

And even if it could, we probably wouldn't want it to - say `i` is initialized to `0`, but `0` is a valid file descriptor, and UNIX actually uses `-1` for file descriptor nullness.

Therefore, we must first roll up a wrapper type:

```cpp
// Intentionally non-RAII
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
it can literally be anything:

```cpp
using unique_fd = std::unique_ptr<int,             fd_closer>; // Ok
using unique_fd = std::unique_ptr<file_descriptor, fd_closer>; // Ok
using unique_fd = std::unique_ptr<void,            fd_closer>; // Ok
```

... Even though they are all not quite right. We are not managing `int` or `file_descriptor` or `void`, but rather some UNIX file that is hidden from us.

For readablity, it's worth to make a opaque type for it:

```cpp
struct unix_file;
using unique_fd = std::unique_ptr<unix_file,       fd_closer>; // Ok, recommended
```

The takeaway is: we can think of `Deleter::pointer` as a handle that refers to some resource.

On a side note, there's a gotcha our `unique_fd`:

```cpp
unique_fd fd{STDIN_FILENO}; // `fd` initialized to -1!
// Because STDIN_FILENO expands to 0,
// and a literal zero is also nullptr;
// the overload unique_ptr(nullptr) is selected

unique_fd fd{int(STDIN_FILENO)}; // Now selects unique_ptr(pointer) due to implicit conversion,
// and initialized to 0 as intended

unique_fd fd{file_descriptor(STDIN_FILENO)}; // Ok too
```

This is one of those "C++ being C++" moments and why I have a love/hate abusive relationship with it.
Anyway, as suggested above, providing a factory function for your RAII class minimizes pitfalls like this.


## `unique_ptr` is not all there is

So far we've shown that `unique_ptr` can be customized to manage resources that are referred to by some handle.
But it is not the most general form of RAII.
Sometimes there isn't any resource to manage, at least not explicitly;
we just desire to do some rollback when some operation fails, for example.

There is where `scope_exit` and the like comes in:

```cpp
void sort_database_dangerous();  // may fail and throw, leaving the database in a bad state
void backup_database();          // may fail due to e.g. not enough disk space
void delete_backup() noexcept;   // always safe; if there's no backup, do nothing
void restore_backup() noexcept;  // same as above

void sort_database_safe() {
    std::experimental::scope_exit( [&](){ delete_backup(); } );
    std::experimental::scope_fail( [&](){ restore_backup(); } );
    delete_backup();
    backup_database();
    sort_database_dangerous();
}
```

Popularized by Andrei Alexandrescu,
these handy functions didn't quite make it to the standard yet, hence they are in `<experimental/scope>`.
There are also various third party implementations available.


## Afterword

While it has limitations, `std::unique_ptr` is a great template for us to implement custom RAII classes.
Not only the code is usually more concise and correct, but also we can have a unified API
for managing a great variety of resources in the codebase, and consistency is another win.
