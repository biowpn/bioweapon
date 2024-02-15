---
title: "What the `func` is that?"
date: 2024-01-18
---

## Intro

C++26 is going to be exciting.
On one hand, we will have a lot of new and potentially game-changing features from both the core language and the standard library, and that's not counting the yet-to-land papers (looking at you, *static reflection*).
On the other hand, we have `std::copyable_function` and `std::function_ref`. Mind you, both are already merged into the current working draft, meaning that if nothing major happens, they will end up in C++26.

Assuming so, there will be altogether 4 call wrappers in the C++26 standard library:
- `std::function`
- `std::move_only_function`
- `std::copyable_function`
- `std::function_ref`

I don't know about you, but when I looked at this for the first time, my reaction was:

<img src="https://media1.tenor.com/m/GhD1lb2G9MgAAAAd/ryan-reynolds-wtf.gif" width="100" height="100" />


## But Why?

After the initial shock, I did my research. How did we end up with this mess? I read, carefully, [the paper for `move_only_function`](https://wg21.link/P0288R9), [the paper for `copyable_only_function`](https://wg21.link/P2548R6), and [the paper for `function_ref`](https://wg21.link/P0792R14).

Of them, `function_ref` makes the most sense to me: it is to `function` as `string_view` to `string`. Cheap to copy (and cheap to invoke), it is intended to use on API boundaries - namely, function parameters - just as `string_view` is. ~~So why isn't it named `function_view`~~. And `function_ref` is actually very useful. In many codebases I've worked with, there are closure types like

```cpp
struct data_recv_callback {
    void (*fn)(void* o, const byte* buf, size_t len);
    void* obj;
};
```

`function_ref` is exactly the generic solution I'm looking for. Neat.

What about `copyable_function` and `move_only_function`?

[The paper for `copyable_only_function`](https://wg21.link/P2548R6) (and [cppreference](https://en.cppreference.com/w/cpp/utility/functional)) describes it as "... refinement of move_only_function". As for [the paper for `move_only_function`](https://wg21.link/P0288R9), it doesn't have a "Motivation" section like most other papers, only "Brief History", where it briefly references 3 other papers, and says

> ... made a strong case for the importance of a move-only form of std::function.

As for those 3 other papers in the "References" section:
- The 1st one, *N4543 "A polymorphic wrapper for all Callable objects"*, does have a Motivation section
- The 2nd one, *Bug 34 - Need type-erased wrappers for move-only callable objects*, has broken link
- The 3rd one, *P0288R2 "The Need for std::unique_function"*, gives me 404. I changed R2 to R1 in the URL and now it redirects to P0288R1, which is just the 1st paper N4543 but a later revision.

So I essentially have one paper N4543/P0288R2 to work with. The "Motivation" section is lengthy, but what it says is basically we need non-copyable `function` for the following use cases:
> - Lambdas with move-only capture: `[u = std::move(u)](io_response r)
{r.send_next(u);}`
> - An event dispatching system, for example, might wish to manage ownership of event handler
objects
> - Real-world function objects are expected
to do whatever other objects do ... Non-copyable objects are not uncommon

I'm ... not fully convinced. Even the paper points out that there's workaround for wrapping non-copyable callables in `function`, albeit not ideal, by leveraging `reference_wrapper`.

So I trace one level deeper - the paper that N4543 references, [N4159 `std::function` and Beyond](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4159.pdf), and finally got some answers.


## Where It All Began

[N4159 `std::function` and Beyond](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4159.pdf) was a actually good read. It points out the actual biggest shortcoming of `std::function`:

**Const-correctness and data races**.

In short, this:

```cpp
struct Functor {
    void operator()() { std::cout << "Non-const\n"; }
    void operator()() const { std::cout << "Const\n"; }
};

const Functor ftor;                   // I'm const!
const std::function<void()> f = ftor; // So am I! Const all the way
f();                                  // Prints "Non-const"
```

In fact, there's no way to make the code print "Const".

Well, since `f` makes a copy of `ftor`, it owns a seperate value,
so it makes sense to be able to call the non-const version.

What's surprising is, despite `f` is `const` qualified (and so is its call operator),
it nevertheless invokes its `ftor` as it was non-const.

Probably, what should've happened is:

```cpp
      std::function<void()> f = ftor; f(); // prints "Non-const"
const std::function<void()> f = ftor; f(); // prints "Const"
```

So, `std::function` needs some fixing, or in N4159's words, "repairing". It lists 4 options:

1. Add internal synchronization
2. Require the target type to be const-­callable
3. Add a non­const `operator()`
4. Standardize the status quo

Only a combination of 2 and 3 can make the above "should've happened" happen. And N4159 agrees:

> We recommend requiring the target type to be const­callable, because it leaves std::function
in the most useful, consistent state.

Unfortunately, this means breaking a lot of code, in two of the worst ways possible:

- Valid code before would stop to compile
- Code would silently change behavior at runtime

And given how `std::function` was (and still is) widely used, option 2/3 was a really hard sell to the people with votes.

N4159 adds,

> This would probably break significant amounts of client
code, but we think the broken code could be fixed with trivial local edits in most cases, and in any
event, the alternatives look worse.

But as we know now, the fixes to `std::function` never happen.
To date (and probably forever), `std::function` is suffering constantly from the issue.

There is another issue with `std::function`:
invoking an empty instance will throw an exception `std::bad_function_call`.
Some people argue this behavior is good,
but more people think that it is inconsistent with the rest of the standard library
(using the value from a wrapper in empty state is undefined behavior; e.g., `unique_ptr`, `optional`)
and violates the "Zero overhead abstraction" principle.

I guess, in the end, more people wanted to do something about the problems of `std::function` than maintain the status-quo, but they couldn't directly touch `std::function`.

Therefore,


## Here Be `std::move_only_function`

You are right, the biggest selling point of `std::move_only_function` is **not** move-only as its name suggests,
but rather that it fixes the problems of `std::function`:

1. It is const-correct;
2. No extra check for nullness to raise exception;
3. Due to 2, it can be made `noexcept` if you deem so

Being move-only just increases its application space:
it can bind to move-only callables.

However, some time later, we decided that being copyable is also valuable,
so here comes `std::copyable_function`:
it is actually a fixed version of `std::function`, which is totally not reflected in its name.

**`std::copyable_function` is the chosen replacement for `std::function`.**
It remains a doubt whether it'll happen, though.
Will people replace all of their `std::function` to `std::copyable_function` when upgrading to C++26?

Only in one way would I consider doing it:

```cpp
namespace my {
    using function = std::copyable_function;

    // Pretend function is std::function
}
```


## But Is There Another Way?

When we see

- `std::function`
- `std::move_only_function`
- `std::copyable_function`

They are all `function` wrappers, they serve the same goal of wrapping callables, just with different copyablility.
What if we can add a template parameter to represent the copyablility (or the more general traits), as N4159 similarily suggests:

```cpp
template <class T, bool Copyable>
class function;

template <class R, class... Args, bool Copyable = true>
class function<R(Args...), Copyable>;
```

Or, if `bool` as argument is not readable,

```cpp
template <class T, class Traits>
class function;

inline constexpr struct copyable_t {} copyable;
inline constexpr struct move_only_t {} move_only;
```

This way we get to keep the `function` name at least?

Unfortunately, this means modifying the primary template of `std::function`,
which currently only takes one template parameter:

```cpp
template<class T>
class function;
```

And it would still need to modify the semantics to fix the const correctness bug either way.

With all that said, I do low-key hope that `move_only_function`
and `copyable_function` would have been introduced this way,
perhaps as a new primary template in a sub namespace:

```cpp
namespace std::ranges {

template <class T, class Traits>
class function;

}
```

... I'm only semi joking.


