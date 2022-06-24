#include <iostream>

struct Foo {
    static int s_num_instances;
    
    Foo() {
        s_num_instances++;
    }
    ~Foo() {
        s_num_instances--;
    }
};
int Foo::s_num_instances;

Foo g_foo;
int main() {
    Foo foo;
    std::cout << "num foo's: " << Foo::s_num_instances << "\n";
    return 0;
}
