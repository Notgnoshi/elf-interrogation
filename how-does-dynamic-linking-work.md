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
$ readelf --wide -r concat.o
Relocation section '.rela.text' at offset 0x2a10 contains 1 entry:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000027  0000003a00000004 R_X86_64_PLT32         0000000000000000 _ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EERKS8_SA_ - 4
```

That's gibberish. Add `-C` to demangle the symbol name:

```sh
$ readelf --wide -rC concat.o
Relocation section '.rela.text' at offset 0x2a10 contains 1 entry:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000027  0000003a00000004 R_X86_64_PLT32         0000000000000000 std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > std::operator+<char, std::char_traits<char>, std::allocator<char> >(...) - 4
```

So at offset `0x27` in `.text` there's a reference to `std::operator+` that the linker must resolve.

## Position-independent code

So that explains what a relocation _is_, but it doesn't explain why relocations can cause problems
producing a shared library. The original error message mentioned `R_X86_64_32` and `.rodata`, so
let's take a closer look:

```sh
$ readelf -r concat.o | grep rodata
0000000000000033  000000220000000a R_X86_64_32            0000000000000000 .rodata + 0
```

The `R_X86_64_32` relocation type is an **absolute** address.

If we compile with `-fPIC` as the error message suggested, we get a different relocation type:

```sh
$ g++ -fPIC -c concat.cpp -o concat.o
$ readelf -r concat.o | grep rodata
0000000000000035  0000002200000002 R_X86_64_PC32          0000000000000000 .rodata - 4
```

The `R_X86_64_PC32` relocation type is an address that's **relative** to the program counter
(abbreviated "PC" - it's the address of the current instruction). This is what **position
independent** code is: code that can be loaded at any address.

This is important, because Linux uses **Address Space Layout Randomization** (ASLR) to load programs
and libraries at random addresses as a security mitigation. So we need to compile with `-fPIC` to
ensure that when we refer to a relocation, we do so in a way that's _relative_ to a known address.

```sh
$ g++ -fPIC -shared concat.cpp -o libconcat.so
$ readelf -h libconcat.so | grep Type:
  Type:                              DYN (Shared object file)
```

With `-fPIC`, the libraries and executable build:

```sh
$ g++ -fPIC -shared concat.cpp -o libconcat.so
$ g++ -fPIC -shared greet.cpp -L. -lconcat -o libgreet.so
$ g++ main.cpp -L. -lgreet -lconcat -o greet
```

# Starting the program

If we try to run `./greet`, we get yet another error:

```sh
$ ./greet
./greet: error while loading shared libraries: libgreet.so: cannot open shared object file: No such file or directory
```

So we can see that the `greet` executable is _trying_ to load the `libgreet.so` shared library, but
it can't find it. Before we can understand why, we need to know how the executable is attempting to
load dynamic dependencies.

We can use `strace` to watch `greet` attempt to look for `libgreet.so`:

```sh
$ strace -e open,openat ./greet
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
openat(AT_FDCWD, "/lib64/glibc-hwcaps/x86-64-v4/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/lib64/glibc-hwcaps/x86-64-v3/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/lib64/glibc-hwcaps/x86-64-v2/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/lib64/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/usr/lib64/glibc-hwcaps/x86-64-v4/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/usr/lib64/glibc-hwcaps/x86-64-v3/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/usr/lib64/glibc-hwcaps/x86-64-v2/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/usr/lib64/libgreet.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
./greet: error while loading shared libraries: libgreet.so: cannot open shared object file: No such file or directory
+++ exited with 127 +++
```

Now, our `main()` function in `main.cpp` didn't have _any_ kind of code to do this loading, so how
is it happening?

On Linux, with ELF binaries, this is done by the **program interpreter** a.k.a. "dynamic
interpreter" a.k.a. "dynamic loader" a.k.a. "dynamic linker". The path to the dynamic linker is
hard-coded in every ELF executable, and it cannot be overridden. We can use `readelf` to see what it
is:

```sh
$ readelf -p .interp greet
String dump of section '.interp':
  [     0]  /lib64/ld-linux-x86-64.so.2
```

We can refer to the [ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html) man page for tons
more information on what `ld.so` is, and how we can customize its behavior.

When we run `./greet`, the first thing the Linux kernel does is read the `PT_INTERP` program header
to find the dynamic interpreter, and then it uses that dynamic interpreter to load the executable
and all of its dependencies. So the code that gets executed first when we run `./greet` is actually
`ld-linux-x86-64.so.2`, not `main()`.

# Finding the libraries

The first thing that `ld-linux.so` does when it loads an executable is to look at the `DT_NEEDED`
entries in the executable's dynamic section to find out what shared libraries it needs to load. We
can use `readelf` to see these entries ourselves:

```sh
$ readelf --dynamic greet | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libgreet.so]
 0x0000000000000001 (NEEDED)             Shared library: [libconcat.so]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

## Library search order

After learning what DSOs it needs to load, the dynamic linker searches for them with in a specific
order:

1. `DT_RPATH` specified in the executable
2. `LD_LIBRARY_PATH` environment variable
3. `DT_RUNPATH` specified in the executable
4. `ld.so.cache` cache managed by `ldconfig`
5. Default search paths

The `LD_LIBRARY_PATH` environment variable is often the easiest way to get a library loaded, but
it's intended to allow users the ability to override library lookup, so if an application depends on
it, that can be fragile.

We can use it to get the `greet` executable to find `libgreet.so` and `libconcat.so`:

```sh
$ LD_LIBRARY_PATH=. ./greet
> bob
hello, bob
```

## Debugging library search failures

When troubleshooting library search failures, there are a few tools worth knowing about. The
[ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html) man page lists the environment
variables (of which `LD_LIBRARY_PATH` is one) that can tweak `ld.so`s behavior.

The variables that are helpful for library search issues are:

* `LD_TRACE_LOADED_OBJECTS=1`, which prints libraries as they are loaded, and then exits. The
  application being traced does not execute its `main()` function
* `LD_DEBUG=libs`, which prints the paths that the dynamic loader attempted to load DSOs from. There
  are other values you can pass to `LD_DEBUG` to debug other aspects of the dynamic interpreter.
* `LD_DEBUG_OUTPUT=some/path.txt`, which saves the output from `LD_DEBUG` to the given file instead
  of printing to stderr.

If we use `LD_TRACE_LOADED_OBJECTS` on our executable, it shows the libraries that `ld.so` _was_
able to find, as well as the two `libgreet.so` and `libconcat.so` that it failed to find:

```sh
$ LD_TRACE_LOADED_OBJECTS=1 ./greet
    linux-vdso.so.1 (0x00007f88de551000)
    libgreet.so => not found
    libconcat.so => not found
    libstdc++.so.6 => /lib64/libstdc++.so.6 (0x00007f88de200000)
    libm.so.6 => /lib64/libm.so.6 (0x00007f88de0e9000)
    libgcc_s.so.1 => /lib64/libgcc_s.so.1 (0x00007f88de4ff000)
    libc.so.6 => /lib64/libc.so.6 (0x00007f88ddeee000)
    /lib64/ld-linux-x86-64.so.2 (0x00007f88de553000)
```

This isn't super helpful for troubleshooting _why_ an application can't find libraries, but it _is_
useful for discovering the full transitive dynamic library dependency tree of an application.

If we use `LD_DEBUG`, we can see the dynamic loader attempting to search multiple locations and
fail:

```sh
$ LD_DEBUG=libs ./greet
   1591960:     find library=libgreet.so [0]; searching
   1591960:      search cache=/etc/ld.so.cache
   1591960:      search path=/lib64/glibc-hwcaps/x86-64-v4:/lib64/glibc-hwcaps/x86-64-v3:/lib64/glibc-hwcaps/x86-64-v2:/lib64:/usr/lib64/glibc-hwcaps/x86-64-v4:/usr/lib64/glibc-hwcaps/x86-64-v3:/usr/lib64/glibc-hwcaps/x86-64-v2:/usr/lib64(system search path)
   1591960:       trying file=/lib64/glibc-hwcaps/x86-64-v4/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/lib64/glibc-hwcaps/x86-64-v3/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/lib64/glibc-hwcaps/x86-64-v2/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/lib64/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/usr/lib64/glibc-hwcaps/x86-64-v4/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/usr/lib64/glibc-hwcaps/x86-64-v3/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/usr/lib64/glibc-hwcaps/x86-64-v2/libgreet.so
   1591960:         (no such file)
   1591960:       trying file=/usr/lib64/libgreet.so
   1591960:         (no such file)
   1591960:     
./greet: error while loading shared libraries: libgreet.so: cannot open shared object file: No such file or directory
```

## RPATH and RUNPATH

`RPATH` and `RUNPATH` are two different fields you can set in the ELF binary to specify paths the
dynamic linker should look to find libraries for the application.

* `RPATH` is searched before `LD_LIBRARY_PATH`, and is now considered deprecated because it can't be
  overridden.
* `RUNPATH` can be overridden by `LD_LIBRARY_PATH`, but its value doesn't cascade to transitive
  dependencies like `RPATH` does.

Let's experiment. We can set the `RUNPATH` with the `-rpath` linker flag at build time. We _could_
set it to an absolute path, but then we would lose the ability to move the library / executable
install from machine to machine. So there's a `$ORIGIN` placeholder (the `$` is literal, it's not a
shell variable) that represents the directory the application binary is located in.

```sh
$ g++ main.cpp -L. -lgreet -Wl,-rpath,'$ORIGIN' -o greet
$ readelf --dynamic greet | grep -E 'NEEDED|RUNPATH|RPATH'
 0x0000000000000001 (NEEDED)             Shared library: [libgreet.so]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]
```

Now since `libgreet.so` is in the `greet` executable's `NEEDED` list, the dynamic loader is able to
find `libgreet.so` through the executable's `RUNPATH` without setting `LD_LIBRARY_PATH`:

```sh
$ LD_TRACE_LOADED_OBJECTS=1 ./greet
        linux-vdso.so.1 (0x00007f80a25fd000)
        libgreet.so => /home/nots/src/elf-interrogation/examples/dynamic/libgreet.so (0x00007f80a25f0000)
        libstdc++.so.6 => /lib64/libstdc++.so.6 (0x00007f80a2200000)
        libm.so.6 => /lib64/libm.so.6 (0x00007f80a24bc000)
        libgcc_s.so.1 => /lib64/libgcc_s.so.1 (0x00007f80a21d3000)
        libc.so.6 => /lib64/libc.so.6 (0x00007f80a1fd8000)
        /lib64/ld-linux-x86-64.so.2 (0x00007f80a25ff000)
        libconcat.so => not found
```

_but it still fails to find `libconcat.so`_ because it's a transitive dependency of `libgreet.so`,
and `RUNPATH` only applies to top-level dependencies.

There's two resolutions we could pursue:

1. Set `RUNPATH` on `libgreet.so` in addition to the `greet` executable
2. Use `RPATH` instead of `RUNPATH`

As an example, let's use `RPATH`, but note that if we do so, we will no longer be able to use
`LD_LIBRARY_PATH` to override where the dynamic loader looks for libraries. Since `RPATH` is
deprecated in favor of `RUNPATH`, we have to pass `--disable-new-dtags` to the linker.

```sh
$ g++ main.cpp -L. -lgreet -Wl,--disable-new-dtags,-rpath,'$ORIGIN' -o greet
$ readelf --dynamic greet | grep -E 'NEEDED|RUNPATH|RPATH'
 0x0000000000000001 (NEEDED)             Shared library: [libgreet.so]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN]
```

(notice that `RUNPATH` turned into `RPATH`) and now we're able to locate both libraries when we
execute `greet`:

```sh
$ ./greet
> bob
hello, bob
```

`RPATH` and `RUNPATH` are most common when an application is shipped in a self-contained install
prefix like `/opt/foo/bin/foo.exe` with libraries located in `/opt/foo/lib/`. In this example, you'd
set an `RPATH` of `$ORIGIN/../lib`.

## /etc/ld.so.cache and ldconfig

After the dynamic interpreter attempts to look up a library using `RPATH`, `LD_LIBRARY_PATH`, and
`RUNPATH`, the next location it checks is the `ld.so.cache`. This is typically how most system
libraries are found (searching the file system would be too slow, so we normally try to cache the
library locations).

We can see the `greet` executable finding the `libm.so.6` dependency this way:

```sh
$ LD_DEBUG=libs ./greet
   1610731:     find library=libm.so.6 [0]; searching
   1610731:      search path=/home/nots/src/elf-interrogation/examples/dynamic          (RPATH from file ./greet)
   1610731:       trying file=/home/nots/src/elf-interrogation/examples/dynamic/libm.so.6
   1610731:         (no such file)
   1610731:      search cache=/etc/ld.so.cache
   1610731:       trying file=/lib64/libm.so.6
```

The `ldconfig` command builds this `/etc/ld.so.cache` file, and the `ldconfig -p` command prints it:

```sh
$ ldconfig -p | grep libm.so.6
        libm.so.6 (libc6,x86-64) => /lib64/libm.so.6
```

This cache is _only_ updated by running `ldconfig`, and is _not_ updated as libraries are found. The
`/etc/ld.so.cache` file is owned by root, and it'd be a security hazard to allow any application to
update the cache of where to find libraries for other applications.

`ldconfig` is almost always invoked during package post-install scriptlets, but there's also
sometimes an `ldconfig.service` that's conditionally executed on boot for some systems.

In addition to _speeding up_ library path resolution, `ldconfig` can also cache DSOs installed in
locations _other than_ the default system library paths, making those libraries available _only_
through the cache, and not through the path search mechanism.

This is how library paths like `/usr/lib64/llvm19/lib/` get resolved - that's not a default search
path, but (on my system) `/etc/ld.so.conf.d/llvm19-x86_64.conf` lists `/usr/lib64/llvm19/lib` as a
library path to cache.

## ABI compatibility

The API (Application Programming Interface) is the source-code level contract of a module. The ABI
(Application Binary Interface) is the binary-level contract of a module. It defines the symbol names
(including C++ name mangling), calling convention, stack layout, CPU register usage, and most
importantly, **the size, alignment, and byte offsets of fields within a type**.

The two ideas are related, but separate. We can make a code change that doesn't break the API (it
still compiles just fine) but _does_ break the ABI, meaning both the library that changed, and its
consumers must be rebuilt after an ABI breaking change. As an example, we can add a new
default-constructed field to a struct:

```cpp
// old
struct Options {
    bool reverse;
};
```

If we pass this type into a function like so:

```cpp
std::string concat(const std::string&, const std::string&, Options);
```

and then change the struct definition:

```cpp
// new
struct Options {
    bool reverse;
    char separator = ',';
};
```

That doesn't break the API, but it _does_ break the ABI (the size and layout of the `Options` struct
changed), so anyone using the `Options` struct across a DSO boundary must rebuild since it changed
its layout. Otherwise we'll get silent memory corruption at runtime.

## SONAMEs, and versioned DSO symlinks

**ABI compatibility is of the utmost important for library maintainers to understand.** I've
encountered far too many examples of ABI breakage that sounded innocent in the code review, but
ended up ruining my week afterward.

Dynamic libraries have a mechanism for defining an ABI version, which if used properly, can help us
save ourselves from silent corruption at runtime but hoisting ABI compatibility into the linker
contract at compile time. This is the `SONAME` of a library.

The most common form of a `SONAME` is to suffix the library name with a number. That number is the
ABI version, and is incremented whenever the ABI is broken. `libcrypto.so` from OpenSSL is a decent
example of this:

```sh
$ ls -l /lib64/libcrypto.so*
lrwxrwxrwx. 1 root root   18 Apr 19 19:00 /lib64/libcrypto.so -> libcrypto.so.3.5.5
lrwxrwxrwx. 1 root root   18 Apr 19 19:00 /lib64/libcrypto.so.3 -> libcrypto.so.3.5.5
-rwxr-xr-x. 1 root root 5.6M Apr 19 19:00 /lib64/libcrypto.so.3.5.5
$ readelf --dynamic /lib64/libcrypto.so | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcrypto.so.3]
```

These symlinks are commonly called "Versioned DSOs". These versions are often just the project's
SemVer version, but that's just a convention and not a rule.

The SQLite project is an interesting counterexample. <https://sqlite.org/version3.html> describes
the rationale to include `sqlite3` in the symbol name of every symbol; it allows multiple
ABI-incompatible versions of SQLite to exist in the same binary without symbol conflicts. That's
unusual. Their `SONAME`s ABI version is `0` as a result of them never making a breaking ABI change,
which is very remarkable.

```sh
$ ls -l /lib64/libsqlite*
lrwxrwxrwx. 1 root root   20 Jan 19 18:00 /lib64/libsqlite3.so.0 -> libsqlite3.so.3.51.2
-rwxr-xr-x. 1 root root 1.6M Jan 19 18:00 /lib64/libsqlite3.so.3.51.2
$ readelf --dynamic /lib64/libsqlite3.so.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libsqlite3.so.0]
```

The SQLite maintainers have shown much more ABI compatibility discipline than is normal in my
experience. ABI compatibility can be confusing, and as such it's often unintentionally broken.

We can build our `libgreet.so.1` and `libconcat.so.1` DSOs with a custom SONAME like so:

```sh
$ g++ -fPIC -shared concat.cpp -Wl,-soname,libconcat.so.1 -o libconcat.so.1.0.0
$ ln -sf libconcat.so.1.0.0 libconcat.so.1
$ ln -sf libconcat.so.1 libconcat.so
$ g++ -fPIC -shared greet.cpp -L. -lconcat -Wl,-soname,libgreet.so.1 -o libgreet.so.1.0.0
$ ln -sf libgreet.so.1.0.0 libgreet.so.1
$ ln -sf libgreet.so.1 libgreet.so
$ g++ main.cpp -L. -lgreet -Wl,--disable-new-dtags,-rpath,'$ORIGIN' -o greet
```

Notice that we also made symbolic links from `libgreet.so -> libgreet.so.1 -> libgreet.so.1.0.0`.
This is **very** common. It allows the maintainer of `libgreet.so.1` to produce a bugfix version
`libgreet.so.1.0.1` in such a way that consumers are never aware. It can also allow multiple
versions of the same library to coexist. The `libgreet.so` name is only ever used at build time, and
when it's used, the compiler sees the `SONAME` of `libgreet.so.1`, and uses that in the `greet`
executable's `NEEDED` dependency list:

```sh
$ readelf --dynamic greet | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libgreet.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

Typically package maintainers produce these symlinks in their packaging scripts, and they're shipped
in the package, but it's also possible to produce (some of) these symlinks with `ldconfig -n`,
although this is unusual, and it only includes the `SONAME -> DSO` symlink, not an unversioned
symlink.

# Relocations and the GOT

After the dynamic loader finishes finding and loading the libraries `NEEDED` by an application, it
applies any **relocations** from those libraries.

We saw relocations once already, back in `concat.o`: blanks the linker fills in once it knows where
things live. At runtime with dynamic libraries we have to do the same thing, except now it's the
dynamic loader filling the blanks at runtime, after every library has been given an address.

Because of ASLR, each library lands at a different address every run, so none of those addresses can
be baked in ahead of time. And a shared library's code is mapped into read-only memory, to enable
sharing a single copy across every process using it. So the loader can't patch addresses straight
into the library code without losing that sharing (and without undoing the position independence we
worked for earlier).

As is usually the case in software engineering, we can solve this problem by adding a level of
indirection: the **Global Offset Table**. The GOT is a table of pointers that lives in the library's
_writable_ data. The code never names an external address directly; it reads the address out of a
GOT slot instead. At load time the loader fills each slot with a resolved address. The code itself
is never touched, so it stays read-only and shareable; only the small table differs from one process
to the next.

The clearest example in our program is `std::cout`. The `greet()` function writes to it:

```cpp
std::cout << concat("hello, ", name) << '\n';
```

`std::cout` is a single global object defined inside `libstdc++`, so its address isn't known until
that library is loaded. `libgreet` records a relocation asking the loader to fill in a GOT slot once
it is loaded:

```sh
$ readelf -rC libgreet.so | grep cout
0000000000003fc8  0000000c00000006 R_X86_64_GLOB_DAT      0000000000000000 std::cout@GLIBCXX_3.4 + 0
```

When read as an instruction to the loader `R_X86_64_GLOB_DAT` means: "store the address of
`std::cout` at GOT offset `0x3fc8`." The offset points into the `.got` ELF section, which is nothing
more than an array of 8-byte pointers:

```sh
$ readelf -S libgreet.so | grep '\.got '
  [22] .got              PROGBITS        0000000000003fc0 003fc0 000028 08  WA  0   0  8
```

So when `greet()` runs, it loads a pointer out of slot `0x3fc8` and uses it, and because the loader
filled that slot at startup, the pointer is the real, randomized runtime address of `std::cout`.
This is done at startup, before `main()` is invoked, and before any static constructors are
executed.

If we look at relocations for the `concat(const std::string&, const std::string&)` symbol, we see a
`R_X86_64_JUMP_SLOT`, which is still a GOT relocation, but instead of being eagerly relocated at
startup, it's resolution is deferred to the first time the `concat()` function is called. This is
the default lazy-binding behavior of dynamic function calls.

```sh
$ readelf --wide -rC libgreet.so | grep -w concat
0000000000004008  0000000100000007 R_X86_64_JUMP_SLOT     0000000000000000 concat(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) + 0
```

We'll dig deeper into dynamic function call resolution next.

# Making a dynamic function call

When `greet()` first calls `concat()`, `concat`'s address still isn't in the GOT: its `JUMP_SLOT` is
the lazy kind, so the loader left it unresolved at startup. So then how does the call reach
`concat`?

It goes through the **Procedure Linkage Table** (PLT). The first call hits a PLT function stub that
detours through the dynamic loader, which finds `concat`, writes its address into the GOT slot, and
then jumps to it. Afterwards, every future call reads the filled slot and goes straight there.

Let's watch this happen in GDB.

## The PLT and lazy binding

Run `greet` under `gdb` and stop just before the first call to `concat`. A breakpoint on the `greet`
function does it: `main` reads a line of input, then calls `greet()`, and the call to `concat` lives
inside `greet()`.

```sh
$ gdb ./greet
(gdb) break greet
(gdb) run
> world

Breakpoint 1, 0x00007ffff7fb6632 in greet(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) () from ./libgreet.so
(gdb) disassemble
...
   0x00007ffff7fb666c <+67>:    call   0x7ffff7fb62d0 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@plt>
...
```

The function call doesn't go to `concat`. It goes to `0x7ffff7fb62d0`, which `gdb` labels
`concat@plt` (if you squint hard enough to mentally demangle it). This is a stub function inside
`libgreet` (the calling library).

If we take a look at that stub, we find that it's "only" three instructions:

```sh
(gdb) x/3i 0x7ffff7fb62d0
   0x7ffff7fb62d0 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@plt>:     jmp    QWORD PTR [rip+0x3d32]        # 0x7ffff7fba008 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@got.plt>
   0x7ffff7fb62d6 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@plt+6>:   push   0x1
   0x7ffff7fb62db <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@plt+11>:  jmp    0x7ffff7fb62b0
```

The first instruction jumps to the address stored in `concat`'s GOT slot at `0x7ffff7fba008`. Read
that slot to see where the jump goes:

```sh
(gdb) x/a 0x7ffff7fba008
0x7ffff7fba008 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@got.plt>:    0x7ffff7fb62d6 <_Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_@plt+6>
```

The dynamic loader wrote this entry to the GOT during the relocation phase. It points right back at
the `concat@plt` stub (`concat@plt + 6`), to the `push 0x1` instruction immediately after the first
`jmp`. `0x1` is the `concat` function's relocation index, and then we fall through to the next
`jmp 0x7ffff7fb62b0`. That's the `0x1`th index into the `.rela.plt` table:

```sh
$ readelf -r libgreet.so | sed -n '/.rela.plt/,/^$/p' | head -n 5
Relocation section '.rela.plt' at offset 0x2588 contains 43 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000004000  0000003100000007 R_X86_64_JUMP_SLOT     00000000000008e6 _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_disposeEv + 0
0000000000004008  0000000100000007 R_X86_64_JUMP_SLOT     0000000000000000 _Z6concatRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_ + 0
0000000000004010  0000000200000007 R_X86_64_JUMP_SLOT     0000000000000000 _ZSt17__throw_bad_allocv@GLIBCXX_3.4 + 0
```

Which is exactly the `concat()` function's relocation. So we push the index of the relocation to the
stack, and then jump to `0x7ffff7fb62b0` - which is the `PLT0` entry of the `.plt`, and is shared by
every `@plt` function stub. This is how we eventually call into `_dl_runtime_resolve`, a function
provided by `ld.so`.

Disassembling `PLT0` shows that last hop:

```sh
(gdb) x/3i 0x7ffff7fb62b0
   0x7ffff7fb62b0:      push   QWORD PTR [rip+0x3d3a]        # 0x7ffff7fb9ff0
   0x7ffff7fb62b6:      jmp    QWORD PTR [rip+0x3d3c]        # 0x7ffff7fb9ff8
   0x7ffff7fb62bc:      nop    DWORD PTR [rax+0x0]
```

It pushes `GOT[1]` (`0x7ffff7fb9ff0`) and jumps through `GOT[2]` (`0x7ffff7fb9ff8`). That second
slot holds the resolver:

```sh
(gdb) x/a 0x7ffff7fb9ff8
0x7ffff7fb9ff8: 0x7ffff7fd88a0 <_dl_runtime_resolve_xsavec>
```

`GOT[1]` and `GOT[2]` are reserved slots `ld.so` fills in at startup: `GOT[1]` is `libgreet`'s
`link_map` (which library this is), and `GOT[2]` is the address of `_dl_runtime_resolve`. So
`concat`'s stub pushed the relocation index, `PLT0` pushed the `link_map`, and we arrive at
`_dl_runtime_resolve(link_map, 1)`: everything the resolver needs to look up `concat` and write its
address into the GOT slot.

## LD_BIND_NOW

# Resolving symbols

## .dynsym dynamic symbol table

## Symbol visibility

## Symbol versioning

# More resources
