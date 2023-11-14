
#include <iostream>
#include <source_location>

template <auto p>
void show_me() {
    std::cout << __PRETTY_FUNCTION__ << '\n';
}

struct Point {
    int x, y, z;
};

Point g_point;

int main() {
    auto& [_1, _2, _3] = g_point;
    show_me<&_1>();
    show_me<&_2>();
    show_me<&_3>();
}
