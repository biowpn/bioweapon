
#include <concepts>
#include <tuple>

struct any_type {
    template <class T>
    operator T() {
    }
};

template <class T>
consteval std::size_t field_count(auto... args) {
    if constexpr (!requires { T{args...}; })
        return sizeof...(args) - 1;
    else
        return field_count<T>(args..., any_type{});
}

template <class T>
constexpr auto tie_as_tuple(T& x) {
    constexpr auto N = field_count<T>();
    if constexpr (N == 0) {
        return std::tie();
    } else if constexpr (N == 1) {
        auto& [_0] = x;
        return std::tie(_0);
    } else if constexpr (N == 2) {
        auto& [_0, _1] = x;
        return std::tie(_0, _1);
    } else if constexpr (N == 3) {
        auto& [_0, _1, _2] = x;
        return std::tie(_0, _1, _2);
    } else {
        static_assert(sizeof(T) != sizeof(T), "Too many fields");
    }
}

int main() {

    struct Bar0 {};
    struct Bar1 {
        int x;
    };
    struct Bar2 {
        int x, y;
    };
    struct Bar3 {
        int x, y, z;
    };

    field_count<Bar0>();
    field_count<Bar1>();
    field_count<Bar2>();
    field_count<Bar3>();

    static_assert(field_count<Bar0>() == 0);

    return 0;
}
