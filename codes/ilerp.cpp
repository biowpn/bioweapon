#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <ratio>
#include <type_traits>

template <class T>
concept unsigned_integer = std::is_unsigned_v<T> && !std::same_as<T, bool>;

template <unsigned_integer T>
struct big_int {
    T lo;
    T hi;
};

template <unsigned_integer T>
constexpr inline auto bits = std::numeric_limits<T>::digits;

template <unsigned_integer T>
constexpr inline auto half_bits = bits<T> / 2;

template <unsigned_integer T>
constexpr T high(T x) {
    return x >> half_bits<T>;
}

template <unsigned_integer T>
constexpr T low(T x) {
    return x << half_bits<T> >> half_bits<T>;
}

template <unsigned_integer T>
constexpr bool get_ith_bit(big_int<T> n, int i) {
    auto part = i < bits<T> ? n.lo : n.hi;
    i = i < bits<T> ? i : (i - bits<T>);
    return (part >> i) & 1;
}

template <unsigned_integer T>
constexpr void set_ith_bit(big_int<T>& n, int i, bool v) {
    auto& part = i < bits<T> ? n.lo : n.hi;
    i = (i < bits<T>) ? i : (i - bits<T>);
    if (v) {
        part |= T(1) << i;
    } else {
        part &= ~(T(1) << i);
    }
}

template <unsigned_integer T>
constexpr big_int<T> left_shift(big_int<T> n, int x) {
    return big_int<T>{
        T(n.lo << T(x)),
        T((n.hi << T(x)) | (n.lo >> T(bits<T> - x))),
    };
}

// Big-integer subtraction.
template <unsigned_integer T>
constexpr big_int<T> big_sub(big_int<T> n, T d) {
    if (n.lo >= d) {
        return {T(n.lo - d), T(n.hi)};
    } else {
        T borrow = d - n.lo;
        return {T(std::numeric_limits<T>::max() - borrow + 1), T(n.hi - 1)};
    }
}

// Big-integer multiplication.
template <unsigned_integer T>
constexpr big_int<T> big_mul(T a, T b) {
    T t = low(a) * low(b);
    T s = high(a) * low(b) + high(t);
    T r = low(a) * high(b) + low(s);
    return {
        .lo = T(low(t) + (r << half_bits<T>)),
        .hi = T(high(a) * high(b) + high(s) + high(r)),
    };
}

// Big-integer division.
// Based on:
// https://en.wikipedia.org/wiki/Division_algorithm#Integer_division_(unsigned)_with_remainder
template <unsigned_integer T>
constexpr T big_div(big_int<T> n, T d) {
    if (n.hi == 0) { // optimization
        return n.lo / d;
    }

    big_int<T> q{0, 0};
    big_int<T> r{0, 0};

    for (auto i = 2 * bits<T>; i-- > 0;) {
        // Left-shift R by 1 bit
        r = left_shift(r, 1);
        // Set the least-significant bit of R equal to bit i of the numerator
        set_ith_bit(r, 0, get_ith_bit(n, i));
        if (r.hi != 0 || r.lo >= d) { // r >= d
            r = big_sub(r, d);
            set_ith_bit(q, i, 1);
        }
    }

    return q.lo;
}

// Generic integral lerp (interpolation only).
template <class Integer, std::intmax_t Num, std::intmax_t Den>
constexpr Integer ilerp(Integer a, Integer b, std::ratio<Num, Den>) noexcept {
    using ratio = typename std::ratio<Num, Den>::type;
    static_assert(std::ratio_greater_equal_v<ratio, std::ratio<0, 1>>,
                  "pos is less than 0");
    static_assert(std::ratio_less_equal_v<ratio, std::ratio<1, 1>>,
                  "pos is greater than 1");

    using U = std::uintmax_t;

    int sign = 1;
    U m = a;
    U M = b;
    if (a > b) {
        sign = -1;
        m = b;
        M = a;
    }

    U d = M - m;

    U num = Num;
    U den = Den;
    return a + sign * Integer(big_div(big_mul(d, num), den));
}

// Testing

template <class Integer>
void print_int(Integer x) {
    if constexpr (sizeof(Integer) < sizeof(int)) {
        std::cout << int(x) << "\n";
    } else {
        std::cout << x << "\n";
    }
}

template <class Integer>
void test_ilerp_1(Integer a, Integer b) {
    print_int(ilerp(a, b, std::ratio<0, 4>{}));
    print_int(ilerp(a, b, std::ratio<1, 4>{}));
    print_int(ilerp(a, b, std::ratio<2, 4>{}));
    assert(ilerp(a, b, std::ratio<2, 4>{}) == std::midpoint(a, b));
    print_int(ilerp(a, b, std::ratio<3, 4>{}));
    print_int(ilerp(a, b, std::ratio<4, 4>{}));
}

template <class Integer>
void test_ilerp(Integer a, Integer b) {
    print_int(a);
    print_int(b);
    std::cout << "\n";
    test_ilerp_1(a, b);
    std::cout << "\n";
    test_ilerp_1(b, a);
    std::cout << "\n\n";
}

int main() {
    test_ilerp(INT8_MIN, INT8_MAX);
    test_ilerp(uint8_t(0), uint8_t(UINT8_MAX));
    test_ilerp(INT16_MIN, INT16_MAX);
    test_ilerp(uint16_t(0), uint16_t(UINT16_MAX));
    test_ilerp(INT32_MIN, INT32_MAX);
    test_ilerp(uint32_t(0), UINT32_MAX);
    test_ilerp(INTMAX_MIN, INTMAX_MAX);
    test_ilerp(uintmax_t(0), UINTMAX_MAX);
}
