---
title: "Custom Iterator"
date: 2023-06-29
---


## Intro

In C++, to read each line of an open file, the idiomatic way is:

```cpp
std::ifstream ifs("hello.txt");
for (std::string line; std::getline(ifs, line);) {
    // do something with `line`
}
```

It's simple and straightforward, but doesn't compose well with the range-based algorithms of the standard library. That is because a file is not a range, although conceptually we can treat it as a range of strings. For example, in Python:

```py
for line in open("hello.txt"):
    # do something with `line`
```

We can achieve the same thing in C++ with custom iterators.

Writing conforming iterators, unfortunately, involves some boilerplate code.
This post provides some templates to help us with that (assuming C++ 20).



## Input Iterator

Reusing our intro example, a line-iterator:

```cpp
// Iterate lines through a std::istream by std::getline.
class line_iterator {
public:
    using difference_type = int;
    using value_type = std::string;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::input_iterator_tag;

    static struct sentinel_type {} sentinel;

private:
    std::istream* is_;
    value_type value_;

    // business logic for getting the next value
    void next() {
        std::getline(*is_, value_);
    }

    // business logic for checking whether there is no more value
    bool done() const {
        return is_->eof();
    }

public:
    line_iterator(std::istream& is) : is_(&is) { this->next(); }

    const value_type& operator*() const { return value_; }

    line_iterator& operator++() {
        this->next();
        return *this;
    }
    line_iterator operator++(int) {
        auto old = *this;
        ++(*this);
        return old;
    }

    bool operator==(sentinel_type) const { return this->done(); }
};
```

And the accompanying range:

```cpp
class iter_line {
private:
    std::istream& is_;

public:
    iter_line(std::istream& is) : is_(is) {}

    auto begin() { return line_iterator(is_); }

    auto end() { return line_iterator::sentinel; }
};
```

So we can write:

```cpp
std::ifstream ifs("hello.txt");
for (auto& line: iter_line(ifs)) {
    // do something with `line`
}
```

Note that the following checks would pass:

```cpp
static_assert(std::input_iterator<line_iterator>);
static_assert(std::ranges::input_range<iter_line>);
static_assert(
    std::is_same_v<std::iterator_traits<line_iterator>::iterator_category,
                    std::input_iterator_tag>);
```



## Output Iterator

Output iterators are very different from input iterators. In fact, they are *barely* iterators;
They are more like adaptors that wrap the actual function for output in a iterator class.

Let's write an output iterator that adds prefix and postfix (the `std::ostream_iterator` adds postfix only):

```cpp
// Prefix and postfix value output to std::ostream using operator<<.
class ostream_iterator_2 {
public:
    using difference_type = int;
    using iterator_category = std::output_iterator_tag;

private:
    std::ostream* os_;
    std::string prefix_;
    std::string postfix_;

    // business logic for outputting a value
    template <class T>
    void output(const T& x) {
        (*os_) << prefix_ << x << postfix_;
    }

public:
    ostream_iterator_2(std::ostream& os, std::string prefix, std::string postfix)
        : os_(&os), prefix_(std::move(prefix)), postfix_(std::move(postfix)) {}

    ostream_iterator_2& operator*() { return *this; }
    ostream_iterator_2& operator++() { return *this; }
    ostream_iterator_2& operator++(int) { return *this; }

    template <class T>
    ostream_iterator_2& operator=(const T& x) {
        output(x);
        return *this;
    }
};
```

With `line_iterator` and `ostream_iterator_2`, we can write:

```cpp
// prepend filename to each line of the input file, and print to stdout
const char* filename = "input_file.txt";
std::ifstream ifs(filename);
std::ranges::copy(iter_line(ifs), ostream_iterator_2(std::cout, filename + std::string(": "), "\n"));
```



## Output Iterator using `std::back_insert_iterator`

An alternative to writing ad-hoc output iterators is to reuse `std::back_insert_iterator`.
Simply add the following to your class that will accept the output value:

```cpp
class my_application {
  public:
    // our payload
    using value_type = std::string;
    
    // called to supply a new value
    void push_back(const value_type&);
};
```

The above example can be rewritten as:

```cpp
// Prefix and postfix value output to std::ostream using operator<<.
class printer {
private:
    std::ostream* os_;
    std::string prefix_;
    std::string postfix_;

    // business logic for outputting a value
    template <class T>
    void output(const T& x) {
        (*os_) << prefix_ << x << postfix_;
    }

public:
    printer(std::ostream& os, std::string prefix, std::string postfix)
        : os_(&os), prefix_(std::move(prefix)), postfix_(std::move(postfix)) {}

    using value_type = std::string;

    void push_back(const value_type& x) {
        output(x);
    }
};


// prepend filename to each line of the input file, and print to stdout
const char* filename = "input_file.txt";
std::ifstream ifs(filename);
printer p(std::cout, filename + std::string(": "), "\n");
std::ranges::copy(iter_line(ifs), std::back_inserter(p));
```



## Afterword

Using custom iterators can help improve our code quality; getting data is now separated from processing data (algorithms) and can be tested separately.
