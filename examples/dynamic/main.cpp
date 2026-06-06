#include "greet.hpp"

#include <iostream>

int main() {
    std::string name;
    while (std::cout << "> " && std::getline(std::cin, name) && !name.empty()) {
        greet(name);
    }
}
