

void g(int) {
}

template <class T = int>
struct B {
    B(T) {
    }
};

namespace my {
struct Bar {};

void f(Bar) {
}
} // namespace my

void fun(int(x), int(y));

int main() {
    {
        struct f {
            f(int) {
            }
        };

        int x = 0;
        auto a = f(x);
        (f(x));
    }

    {
        void g(int);

        struct F {
            using Fun_Pointer = void (*)(int);
            operator Fun_Pointer() {
                return g;
            }
        };

        F f;
        int x = 0;
        f(x); // g(x)
    }

    {
        struct Bar {};
        Bar b;

        // int Bar = 0; // Bar is a variable now!
        class Bar b2;
    }

    {
        my::Bar x;
        f(x);
    }

    {
        class T {
            int x;
        } object;       // An object ...
        T* x = &object; // and a pointer to it

        using f = const unsigned char*;
        auto p = f(x);
    }
}
