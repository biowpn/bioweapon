#include <algorithm>
#include <string>
#include <vector>

namespace gem {
    namespace nested {
        template <class T> struct bar_traits;
    }
    using namespace nested;
}

template <>
struct gem::bar_traits<int> {};

int main() {
    return 0;
}
