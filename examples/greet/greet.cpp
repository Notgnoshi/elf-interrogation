#include <unistd.h>

ssize_t greet() {
    static const char* greeting = "Hello world.\n";
    static const size_t greeting_length = 13;

    return write(STDOUT_FILENO, greeting, greeting_length);
}

int main() {
    return greet();
}
