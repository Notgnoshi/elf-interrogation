#include <stdlib.h>
#include <unistd.h>

int main() {
    static const size_t element_size = 1; // 1 byte per element
    static const size_t elements = 10;    // 10 elements

    // Bracket the heap operations in write(2) syscalls so we can see them easier with strace(1)
    write(STDOUT_FILENO, "Before calloc\n", 14);
    void *heap_buffer = calloc(elements, element_size);
    write(STDOUT_FILENO, "After calloc\n", 13);
    free(heap_buffer);
    write(STDOUT_FILENO, "After free\n", 11);
    return 0;
}
