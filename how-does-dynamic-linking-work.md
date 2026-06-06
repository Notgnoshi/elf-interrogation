# How does dynamic linking work?

In general, there are two kinds of libraries that applications can depend on to facilitate code
reuse:

* **Static libraries** are bundled into the program at build time, and the resulting executable
  contains all of the code it needs to run.
* **Dynamic libraries** are not bundled into the program at build time. Instead the executable
  contains references to the libraries that are resolved at runtime. This allows sharing a single
  library across multiple programs, and it allows updating the library without rebuilding the
  programs that depend on it (so long as the new library is compatible with the old one).

This document will attempt to provide a ground-up understanding of how dynamic linking works, with a
focus on common troubleshooting tools and failure scenarios. The exact details of how dynamic
linking works varies between operating systems (this document is Linux-specific) but the general
principles apply equally across all platforms.

# An example application

Our example is a chain of dynamic function calls:

```mermaid
flowchart LR
    greet["greet"] -->|"greet(name)"| libgreet["libgreet.so"]
    libgreet -->|"concat(a, b)"| libconcat["libconcat.so"]
```

Both `libgreet.so` and `libconcat.so` will be implemented by a single header+source file translation
unit.

```cpp
// concat.hpp
#pragma once
#include <string>

std::string concat(const std::string& a, const std::string& b);
```

```cpp
// concat.cpp
#include "concat.hpp"

std::string concat(const std::string& a, const std::string& b) {
    return a + b;
}
```

```cpp
// greet.hpp
#pragma once
#include <string>

void greet(const std::string& name);
```

```cpp
// greet.cpp
#include "greet.hpp"
#include "concat.hpp"

#include <iostream>

void greet(const std::string& name) {
    std::cout << concat("hello, ", name) << '\n';
}
```

And then the `greet` executable will prompt for input and call through to the dynamic `greet()`
function:

```cpp
// main.cpp
#include "greet.hpp"

#include <iostream>

int main() {
    std::string name;
    while (std::cout << "> " && std::getline(std::cin, name) && !name.empty()) {
        greet(name);
    }
}
```

But building `libconcat.so` the obvious way fails:

```sh
$ g++ -shared concat.cpp -o libconcat.so
/usr/bin/ld.bfd: /tmp/ccCIUL4s.o: relocation R_X86_64_32 against `.rodata' can not be used when making a shared object; recompile with -fPIC
/usr/bin/ld.bfd: failed to set dynamic section sizes: bad value
collect2: error: ld returned 1 exit status
```

This error presumably has something to do with a **relocation**. What is that all about?

## Relocations

Compile one source file on its own and you get a _relocatable object_ (`.o`):

```sh
$ g++ -c concat.cpp -o concat.o
$ readelf -h concat.o | grep Type:
  Type:                              REL (Relocatable file)
```

A **relocatable object** is one that has placeholders for addresses that are not yet known. The
`concat(a, b)` function is an example of this: it refers to `std::operator+()` to concatenate two
strings, but `std::operator+()` is defined in `libstdc++`, not in `concat.cpp`, so the compiler
cannot resolve it immediately.

Instead, it leaves a **relocation** placeholder:

```sh
$ readelf -r concat.o
Relocation section '.rela.text' at offset 0x2a10 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000027  003a00000004 R_X86_64_PLT32    0000000000000000 _ZStplIcSt11char_[...] - 4
```

That looks like nonsense, and it's truncated. Add `-C` to demangle the symbol name and `--wide` so
it isn't cut off:

```sh
$ readelf -rC --wide concat.o
Relocation section '.rela.text' at offset 0x2a10 contains 1 entry:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000027  0000003a00000004 R_X86_64_PLT32         0000000000000000 std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > std::operator+<char, std::char_traits<char>, std::allocator<char> >(...) - 4
```

So at offset `0x27` in `.text` there's a reference to `std::operator+` that the linker must resolve.
