---
title: "Heterogeneous Message Handler"
date: 2025-08-20
---


## Intro

Consider a system where data is passed between components via a variety of messages:

```cpp
/// File: messages.hpp

struct MessageA { ... };
struct MessageB { ... };
struct MessageC { ... };
struct MessageD { ... };
struct MessageE { ... };
```

A component, `Widget`, needs to handle these messages:

```cpp
/// File: Widget.hpp

/// This class needs to handle Message[ABCDE].
class Widget {
    ...
};
```



## Handling Messages

What are some ways to organize the message handling code within `Widget`?



### Uniquely Named Function

One way is to have differently spelled functions for each message:

```cpp
/// File: Widget.hpp

#include <messages.hpp>

class Widget {
    void handleA(const MessageA&);
    void handleB(const MessageB&);
    void handleC(const MessageC&);
    void handleD(const MessageD&);
    void handleE(const MessageE&);
};
```

While C-style, this is friendly to code searching/reading, thanks to the unique function names.

But wait, C++ has overloads, surely we can make use of it:



### Overload

```cpp
/// File: Widget.hpp

#include <messages.hpp>

class Widget {
    void handle(const MessageA&);
    void handle(const MessageB&);
    void handle(const MessageC&);
    void handle(const MessageD&);
    void handle(const MessageE&);
};
```

This makes the function names shorter, and a unified function name is better for generic code.

However, both the uniquely named function and the overload solutions require declarations of the messages in the class definition,
so you'd have to either `#include <messages.h>` in the class header, or forward-declare them, or use the pimpl technique.

- There is a "slick" way of forward-declaration - within the function signature:

    ```cpp
        void handle(const struct MessageA&);
        //                ^^^^^^
    ```

The overload approach comes with a potential caveat. Say at some point of time, a new message type `MessageF` is introduced,
but it is a subclass of an existing message type:

```cpp
struct MessageF : MessageE { ... };
```

We would like all components (imagine there are many `Widget`-like classes) to explicitly handle this new message. With the overload approach, `MessageF` would silently fall back to `handle(const MessageE&)`, which is undesirable.

- It's a separate debate whether messages *should* inherit one another. In general, composition is preferred over inheritance, but due to practicalities, inheritance between messages arises and some have valid use cases.

And this is where the uniquely named function approach wins in this area. Without `handleF` defined, `w.handleF(msg)` will be a hard compile error.

Is there another way to achieve encapsulation without boiler plate, while avoiding the pitfall of silent fallback?



### Template

Enter member template approach:

```cpp
/// File: Widget.hpp

class Widget {
    template <class T>
    void handle(const T&) = delete("Unhandled message");
};
```

```cpp
/// File: Widget.cpp

#include <Widget.hpp>
#include <messages.hpp>

template <> void Widget::handle(const MessageA&) { ... }
template <> void Widget::handle(const MessageB&) { ... }
template <> void Widget::handle(const MessageC&) { ... }
template <> void Widget::handle(const MessageD&) { ... }
template <> void Widget::handle(const MessageE&) { ... }
```

This eliminates boilerplate, as each message is typed only once.
It also avoids the silent fallback pitfall, due to how template argument deduction works - the `T` must be exact:

```cpp
struct Base {};

struct Derive : Base {};

template <class T>
void f(const T&) { static_assert(false, "primary is chosen"); }

template <>
void f(const Base&) {}

int main() {
    Derive d;
    f(d);  // error: static assertion failed: primary is chosen
}
```

It has its own gotchas, though: the specializations must be visible before they are used, otherwise they won't be selected. Member templates aren't exception to this rule.



## Dispatching Messages

A closely related question is: how to dispatch messages based on type and pass them to the correct handler?

A typical scenario is that these messages are sent in a type-erased way to the handler: from a file, over the network, through some inter-process channel, etc. In either case, the application just sees a sequence of bytes, and needs to recover the message type based on runtime data.

One design is that the messages share a data member, a discriminator, whose offset is the same across all messages. For example, the first byte as a character is used to indicate the message type:

```cpp
struct MessageA { char type; ... };  // `type` is always 'A'
struct MessageB { char type; ... };  // `type` is always 'B'
struct MessageC { char type; ... };  // `type` is always 'C'
struct MessageD { char type; ... };  // `type` is always 'D'
struct MessageE { char type; ... };  // `type` is always 'E'
```

For systems designed this way, the `Widget` would have a generic `handle(std::span<const std::byte> msg)` that does something like this:

```cpp
void Widget::handle(std::span<const std::byte> msg) {
    const auto type = static_cast<char>(msg[0]);
    switch (type) {
        case 'A':
            return handle(*reinterpret_cast<const MessageA*>(msg.data()));
        case 'B':
            return handle(*reinterpret_cast<const MessageB*>(msg.data()));
        case 'C':
            return handle(*reinterpret_cast<const MessageC*>(msg.data()));
        case 'D':
            return handle(*reinterpret_cast<const MessageD*>(msg.data()));
        case 'E':
            return handle(*reinterpret_cast<const MessageE*>(msg.data()));
        default:
            ...error handling...
    }
}
```

- Note: the above code does not handle alignment.

As we can see, there is some boilerplate. We could use macro to eliminate the duplication, or, if you recall the [Type Sequence post](https://biowpn.github.io/bioweapon/2023/10/05/type-sequence-factory-pattern.html), we could use a type list and some meta-programming trick to achieve the same. C++26 reflection offers even more ways to do this.

With system designed this way, if macros are used, then none of three approaches is favored, since even with uniquely named function approach the function name can be synthesized via token-pasting. Otherwise, overload and template plays very nicely with a type list based approach, since the name `handle` stays the same.

Alternatively, the system may provide runtime type-dispatching functions outside `Widget`. For example, it can pass a `std::variant` of messages to `Widget`, and the type-dispatching can be done via `std::visit`:

```cpp
using AnyMessage = std::variant<MessageA, MessageB, MessageC, MessageD, MessageE>;

void Widget::handle(const AnyMessage& any_msg) {
    std::visit([this](const auto& msg) { handle(msg); }, any_msg);
}
```

- It would be very nice if we can write `std::visit(handle, any_msg)`. Alas, short-hand closure and overload set type are not a thing. Best we can do is `std::visit(OVERLOAD(handle), any_msg)` where `OVERLOAD` is a macro that expands to the lambda

With system designed this way, the overload approach and template approach are favored for the same reason - `handle` is uniformly named across all messages.



## Summary

We explored three different ways to organize the code for a heterogeneous message handler. While the template approach seems like the overall winner, the uniquely named function approach offers some trade off for better readability and searchability. The overload approach doesn't bring much new to the table and should be avoided.

Could there be an even better way? Let me know!
