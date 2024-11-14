#include <iostream>

template <class T = int>
struct f {
    f()  { std::cout << "1"; }
    f(T) { std::cout << "2"; }
    ~f() { std::cout << "3"; }
};

int x = 0;

int main() {
    f(x);
}