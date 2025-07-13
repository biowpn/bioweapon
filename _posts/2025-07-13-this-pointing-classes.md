---
title: "This-pointing Classes"
date: 2025-07-13
---


## Intro

Consider the following class where a data member is a pointer to the instance itself:

```cpp
class OneIndirection {
    OneIndirection* ptr_ = this;

  public:
    void foo() {
        ptr_->foo_impl();
    }

    void foo_impl() {
        // ...
    }
};
```

The reason why we would want such a class is not the interest of this post.

- Notwithstanding, if `ptr_` *always* equals `this`, then it's pretty pointless. But if `ptr_` could point to other instances (essentially a form of instance-proxying), then this trick could be useful.

What is of the interest is: how do you move `OneIndirection`?

```cpp
OneIndirection(OneIndirection&& other) noexcept {
    // what goes here?
}
```



## The "trivial" move

As it stands, the above skeleton code **is** the correct implementation! Presuming `ptr_` is the only data member of `OneIndirection`.

Likewise, the correct move-assignment implementation should be:

```cpp
OneIndirection& operator=(OneIndirection&& other) noexcept {
    return *this;
}
```

It may not seem intuitive at first, but remember the class invariant is: `ptr_` should *always* point to the current instance; in other words, it should *always* equal to `this`. Assignment does not change the address of the current instance (in fact, nothing does; the address of an object is stable throughout its lifetime), so `ptr_` should not be touched.

I'd also like to note about `trivially_movable`. Had we not provided the moving operations, the compiler would generate them for us (and would consider the class trivially movable), which would do something equivalent to bitwise `memcpy`:

```cpp
// A compiler-generated move-assignment-operator would do:
OneIndirection& operator=(OneIndirection&& other) noexcept {
    ptr_ = other.ptr_;  // WRONG!
    return *this;
}
```



## A more real example

The `OneIndirection` class is pretty contrived. A more real life example is some circular buffer data structure where the data is stored in an embedded array:

```cpp
template <class T>
class static_circular_buffer {
    T  buffer_[64];
    T* head_ = &buffer_[0];
    ...
};
```

While `head_` doesn't point to the instance itself, its value depends on the address of the instance, hence extra care must be taken when it comes to special member operations.

This:

```cpp
head_ = other.head_;
```

is wrong.

It should look like

```cpp
head_ = buffer_ + (other.head_ - other.buffer_);
```

Having a data member whose value depends on instance address (dubbed **"this-pointing"**) is not rare. It naturally appears in Small Object Optimization, including Small String Optimization for `std::string` in major implementations.



## A more subtle example

Some classes store callbacks that contain a reference to themselves as data member:

```cpp
class Widget {
    std::function<void()> foo_fn_;

  public:
    void foo1();
    void foo2();

    Widget() {
        foo_fn_ = [&]() { foo1(); };
        // or
        foo_fn_ = [this]() { foo2(); };
    }

    void foo() {
        foo_fn_();
    }
};
```

At a glance, `foo_fn_`'s lifetime is within the lifetime of the outer object, so `foo_fn_()` shouldn't run into dangling reference issues.

Well, that is true until this instance is copied / moved. Again, as-is, the class `Widget` is movable (and copyable), but will do the wrong thing:

```cpp
Widget w1;

{
    Widget w2;
    w1 = std::move(w2);
    // w1.foo_fn_ == w2.foo_fn_
    // w1.foo_fn_ now references w2, not w1
}

w1.foo();  // Boom!
```

Sometimes, the move is not even as obvious as `std::move`. I've run into real bug before where there was a `vector<Widget> widgets`. When the vector resizes, move-assignment happens under the hood.

- The bug was more annoying because resizing is out of your control - it's implementation detail; so 3 or 4 widgets may be fine, but 5 widgets suddenly crashes the program

With the earlier example `OneIndirection` and `static_circular_buffer`, it was still possible to provide a correct move operations. With `Widget`, however, it is not. There is no compliant way to access the captured data from `std::function`.

- Even the new `std::function_ref` does not provide access to the thunk pointer and bound entity, probably for good reasons such as encapsulation

To prevent bugs for `Widget`, the only safe way is to ban move (and copy) operations altogether:

```cpp
class Widget {
    Widget(Widget&&) noexcept = delete;
    Widget& operator=(Widget&&) noexcept = delete;
    ...
};
```

This way, `vector<Widget> widgets` stops compiling, which is good. `list<Widget>` will still work correctly.



## Takeaway

Whenever dealing with this-pointing classes (read: data member depends on the address of this instance), be extra mindful with their move and copy operations. If the correct move operation is not possible to implement, consider `delete` them.
