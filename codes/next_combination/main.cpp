
#include <algorithm>
#include <iostream>
#include <string>

#include "impl/biowpn.hpp"
#include "impl/bronnimann.hpp"
#include "impl/hinnant.hpp"

void test_and_show(std::string str, size_t r) {
    using bronnimann::next_combination;

    std::cout << "combinations(" << str << ", " << r << "):\n";
    do {
        std::cout << "  " << str << '\n';
    } while (next_combination(str.begin(), str.begin() + r, str.end()));
    std::cout << "\n";
}

int main() {
    test_and_show("ABCDE", 0);
    test_and_show("ABCDE", 1);
    test_and_show("ABCDE", 2);
    test_and_show("ABCDE", 3);
    test_and_show("ABCDE", 4);
    test_and_show("ABCDE", 5);
}
