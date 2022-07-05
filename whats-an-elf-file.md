# What's an ELF file?
And how do I peek under the hood?

# Contents

- [What's an ELF file?](#whats-an-elf-file)
- [Introduction](#introduction)
- [An example](#an-example)
    - [Compilation pipeline refresher](#compilation-pipeline-refresher)
- [ELF overview](#elf-overview)
    - [File header](#file-header)
    - [Section headers](#section-headers)
    - [Sections](#sections)
    - [Program headers and segments](#program-headers-and-segments)
- [And now welcome to the segment I like to call "What could possibly go wrong?"](#and-now-welcome-to-the-segment-i-like-to-call-what-could-possibly-go-wrong)
    - [String table](#string-table)
    - [Symbol table](#symbol-table)
    - [Stripping](#stripping)
    - [Code](#code)
    - [Data](#data)
        - [Static constants](#static-constants)
        - [Local constants](#local-constants)
        - [Globals / static locals](#globals-static-locals)
    - [Relocations](#relocations)
    - [Dynamic symbols](#dynamic-symbols)
    - [DT_RPATH and DT_BIND_NOW?](#dt_rpath-and-dt_bind_now)
    - [Metadata](#metadata)
    - [Debug symbols](#debug-symbols)
    - [How to customize sections with GCC?](#how-to-customize-sections-with-gcc)

# Introduction

The [ELF (Executable and Linking Format) file format](https://refspecs.linuxbase.org/elf/elf.pdf) is
used for a large number of binary files used to describe executables, libraries, and process memory,
as well as some ancillary formats:

* object files
* executables
* dynamic libraries
* static libraries (`ar(1)` archives of object files)
* coredumps
* debug symbols

# An example

```cpp
int main() { return 0; }
```

## Compilation pipeline refresher

1. Preprocess
   ```sh
   cpp empty.cpp -o empty-preprocessed.cpp
   # equivalently
   g++ -E empty.cppp -o empty-preprocessed.cpp
   ```
2. Compile
   ```sh
   g++ -S empty-preprocessed.cpp -o empty.S
   ```
3. Assemble
   ```sh
   as -c empty.S -o empty.o
   # equivalently
   g++ -c empty.S -o empty.o
   ```
4. Link
    1. For normal people:
       ```sh
       g++ empty.o -o empty
       ```
    2. But if you enable verbose output:
       ```sh
       g++ -v empty.o -o empty
       ```
       you see that under the hood, `g++` invokes `ld` with a default set of arguments that are
       _very_ specific to your particular toolchain:
       ```sh
       ld -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
           -o empty \
           -L/usr/lib/gcc/x86_64-linux-gnu/11 -L/usr/lib/gcc/x86_64-linux-gnu/11/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/11/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/x86_64-linux-gnu -L/usr/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/11/../../.. \
           /usr/lib/gcc/x86_64-linux-gnu/11/../../../x86_64-linux-gnu/Scrt1.o /usr/lib/gcc/x86_64-linux-gnu/11/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/11/crtbeginS.o \
           empty.o \
           -lstdc++ -lm -lgcc_s -lgcc -lc -lgcc_s -lgcc \
           /usr/lib/gcc/x86_64-linux-gnu/11/crtendS.o /usr/lib/gcc/x86_64-linux-gnu/11/../../../x86_64-linux-gnu/crtn.o
       ```

```sh
$ file empty.o empty
empty.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
empty:   ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=1125813516236d591944beebe6bc65026a401188, for GNU/Linux 3.2.0, not stripped
```

We'll use this super simple example to dig through what's in an ELF file below.

# ELF overview

Typical of most binary data formats, an ELF file consists of a series of headers and tables, with
the headers having predictable sizes and formats, telling us how to read the various tables.

![ELF Layout](https://upload.wikimedia.org/wikipedia/commons/7/77/Elf-layout--en.svg)

The first header in the file is always the same -- called the file header, or sometimes the ELF
header.

## File header

The file header contains a bunch of metadata about the file, including what _type_ of ELF file it
is, and how to proceed parsing the rest of it.

```sh
$ readelf --file-header empty.o
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              REL (Relocatable file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x0
  Start of program headers:          0 (bytes into file)
  Start of section headers:          448 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           0 (bytes)
  Number of program headers:         0
  Size of section headers:           64 (bytes)
  Number of section headers:         12
  Section header string table index: 11
```

If we compare the ELF header for the `empty.o` object file and the `empty` executable, we can see a
few notable differences:

```sh
$ readelf --file-header empty
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x1040
  Start of program headers:          64 (bytes into file)
  Start of section headers:          13912 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         13
  Size of section headers:           64 (bytes)
  Number of section headers:         29
  Section header string table index: 28
```

The object file doesn't include any **program headers**, and doesn't define the **entry point
address**. Further, the executable has a few extra sections, and _does_ include a program header and
entry point address.

## Section headers

From looking at the file header, we can determine that the meat of the file is stored in things
called sections, defined by a number of **section headers**.

```sh
$ readelf --wide --section-headers empty.o
There are 12 section headers, starting at offset 0x1c0:

Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        0000000000000000 000040 00000f 00  AX  0   0  1
  [ 2] .data             PROGBITS        0000000000000000 00004f 000000 00  WA  0   0  1
  [ 3] .bss              NOBITS          0000000000000000 00004f 000000 00  WA  0   0  1
  [ 4] .comment          PROGBITS        0000000000000000 00004f 000027 01  MS  0   0  1
  [ 5] .note.GNU-stack   PROGBITS        0000000000000000 000076 000000 00      0   0  1
  [ 6] .note.gnu.property NOTE           0000000000000000 000078 000020 00   A  0   0  8
  [ 7] .eh_frame         PROGBITS        0000000000000000 000098 000038 00   A  0   0  8
  [ 8] .rela.eh_frame    RELA            0000000000000000 000140 000018 18   I  9   7  8
  [ 9] .symtab           SYMTAB          0000000000000000 0000d0 000060 18     10   3  8
  [10] .strtab           STRTAB          0000000000000000 000130 000010 00      0   0  1
  [11] .shstrtab         STRTAB          0000000000000000 000158 000067 00      0   0  1
```

Interestingly, we see from the file header:

> ```
> Size of section headers:           64 (bytes)
> ```

from which we can infer that the section name isn't included in the section header. We can confirm
by looking at [elf(5)](https://linux.die.net/man/5/elf) or the
[ELF standard](https://refspecs.linuxbase.org/elf/elf.pdf) if we wanted more details. The section
names are stored in their own string table section:

> ```
> Section header string table index: 11
> ```

The 11th section header:

> ```
> [11] .shstrtab         STRTAB          0000000000000000 000158 000067 00      0   0  1
> ```

So we can dump this string table and confirm that we see the actual section names

```sh
$ readelf --wide --string-dump=.shstrtab empty.o

String dump of section '.shstrtab':
  [     1]  .symtab
  [     9]  .strtab
  [    11]  .shstrtab
  [    1b]  .text
  [    21]  .data
  [    27]  .bss
  [    2c]  .comment
  [    35]  .note.GNU-stack
  [    45]  .note.gnu.property
  [    58]  .rela.eh_frame
```

This isn't normally something that's useful to do because
[readelf(1)](https://linux.die.net/man/1/readelf) is smart enough to parse the `.shstrtab` table in
order to display the section headers with more helpful output.

## Sections

Looking at the section headers, we can tease out some interesting details.

```sh
$ readelf --sections --wide empty.o
There are 12 section headers, starting at offset 0x1c0:

Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        0000000000000000 000040 00000f 00  AX  0   0  1
  [ 2] .data             PROGBITS        0000000000000000 00004f 000000 00  WA  0   0  1
  [ 3] .bss              NOBITS          0000000000000000 00004f 000000 00  WA  0   0  1
  [ 4] .comment          PROGBITS        0000000000000000 00004f 000027 01  MS  0   0  1
  [ 5] .note.GNU-stack   PROGBITS        0000000000000000 000076 000000 00      0   0  1
  [ 6] .note.gnu.property NOTE           0000000000000000 000078 000020 00   A  0   0  8
  [ 7] .eh_frame         PROGBITS        0000000000000000 000098 000038 00   A  0   0  8
  [ 8] .rela.eh_frame    RELA            0000000000000000 000140 000018 18   I  9   7  8
  [ 9] .symtab           SYMTAB          0000000000000000 0000d0 000060 18     10   3  8
  [10] .strtab           STRTAB          0000000000000000 000130 000010 00      0   0  1
  [11] .shstrtab         STRTAB          0000000000000000 000158 000067 00      0   0  1
```

## Program headers and segments

The **Program Header** defines what **Segments** get loaded into memory.
Object files don't have segments, because they don't define how something gets loaded into memory.

```sh
$ readelf --segments empty.o
There are no program headers in this file.
```

That's something an executable (or a shared library) defines.

```sh
$ readelf --segments --wide empty

Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  PHDR           0x000040 0x0000000000000040 0x0000000000000040 0x0002d8 0x0002d8 R   0x8
  INTERP         0x000318 0x0000000000000318 0x0000000000000318 0x00001c 0x00001c R   0x1
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x0005f0 0x0005f0 R   0x1000
  LOAD           0x001000 0x0000000000001000 0x0000000000001000 0x000145 0x000145 R E 0x1000
  LOAD           0x002000 0x0000000000002000 0x0000000000002000 0x0000c4 0x0000c4 R   0x1000
  LOAD           0x002df0 0x0000000000003df0 0x0000000000003df0 0x000220 0x000228 RW  0x1000
  DYNAMIC        0x002e00 0x0000000000003e00 0x0000000000003e00 0x0001c0 0x0001c0 RW  0x8
  NOTE           0x000338 0x0000000000000338 0x0000000000000338 0x000030 0x000030 R   0x8
  NOTE           0x000368 0x0000000000000368 0x0000000000000368 0x000044 0x000044 R   0x4
  GNU_PROPERTY   0x000338 0x0000000000000338 0x0000000000000338 0x000030 0x000030 R   0x8
  GNU_EH_FRAME   0x002004 0x0000000000002004 0x0000000000002004 0x00002c 0x00002c R   0x4
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x10
  GNU_RELRO      0x002df0 0x0000000000003df0 0x0000000000003df0 0x000210 0x000210 R   0x1

 Section to Segment mapping:
  Segment Sections...
   00
   01     .interp
   02     .interp .note.gnu.property .note.gnu.build-id .note.ABI-tag .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn
   03     .init .plt .plt.got .text .fini
   04     .rodata .eh_frame_hdr .eh_frame
   05     .init_array .fini_array .dynamic .got .data .bss
   06     .dynamic
   07     .note.gnu.property
   08     .note.gnu.build-id .note.ABI-tag
   09     .note.gnu.property
   10     .eh_frame_hdr
   11
   12     .init_array .fini_array .dynamic .got
```

This diagram helps summarize what's in an ELF file, and where they're located.

![ELF 101](https://i.imgur.com/EL7lT1i.png)

(source: https://github.com/corkami/pics/tree/master/binary/elf101)

So let's run this application with GDB and start looking at where things are in memory.

```sh
$ gdb --quiet -ex "break main" -ex "run" empty
(gdb) info proc mappings
process 230427
Mapped address spaces:

          Start Addr           End Addr       Size     Offset  Perms  objfile
      0x555555554000     0x555555555000     0x1000        0x0  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555555000     0x555555556000     0x1000     0x1000  r-xp   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555556000     0x555555557000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555557000     0x555555558000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555558000     0x555555559000     0x1000     0x3000  rw-p   /home/nots/Documents/elf-interrogation/examples/empty/empty
(program break)
      0x7ffff7d77000     0x7ffff7d7a000     0x3000        0x0  rw-p
      0x7ffff7d7a000     0x7ffff7da2000    0x28000        0x0  r--p   /usr/lib/x86_64-linux-gnu/libc.so.6
      0x7ffff7da2000     0x7ffff7f37000   0x195000    0x28000  r-xp   /usr/lib/x86_64-linux-gnu/libc.so.6
      0x7ffff7f37000     0x7ffff7f8f000    0x58000   0x1bd000  r--p   /usr/lib/x86_64-linux-gnu/libc.so.6
      0x7ffff7f8f000     0x7ffff7f93000     0x4000   0x214000  r--p   /usr/lib/x86_64-linux-gnu/libc.so.6
      0x7ffff7f93000     0x7ffff7f95000     0x2000   0x218000  rw-p   /usr/lib/x86_64-linux-gnu/libc.so.6
      0x7ffff7f95000     0x7ffff7fa2000     0xd000        0x0  rw-p
      0x7ffff7fbb000     0x7ffff7fbd000     0x2000        0x0  rw-p
      0x7ffff7fbd000     0x7ffff7fc1000     0x4000        0x0  r--p   [vvar]
      0x7ffff7fc1000     0x7ffff7fc3000     0x2000        0x0  r-xp   [vdso]
      0x7ffff7fc3000     0x7ffff7fc5000     0x2000        0x0  r--p   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
      0x7ffff7fc5000     0x7ffff7fef000    0x2a000     0x2000  r-xp   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
      0x7ffff7fef000     0x7ffff7ffa000     0xb000    0x2c000  r--p   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
      0x7ffff7ffb000     0x7ffff7ffd000     0x2000    0x37000  r--p   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
      0x7ffff7ffd000     0x7ffff7fff000     0x2000    0x39000  rw-p   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
      0x7ffffffde000     0x7ffffffff000    0x21000        0x0  rw-p   [stack]
  0xffffffffff600000 0xffffffffff601000     0x1000        0x0  --xp   [vsyscall]
```

All this shows is that there's a few memory regions, and where their addresses are, with a _little_
bit if information on what's loaded into those regions.

```
+--------------+ 0x555555554000
|              |
| ELF Segments |
|              |
+--------------+ 0x555555559000
| Heap         |
+--------------+ 0x555555559000 (program break)
...
+--------------+ 0x7ffff7d77000
|              |
| Loaded DSOs  |
|              |
+--------------+ 0x7ffff7fff000
...
+--------------+ 0x7ffffffde000
|              |
| Stack        |
|              |
+--------------+ 0x7ffffffff000
```

For more information, take a peek at [How are processes loaded into
memory?](./how-are-processes-loaded-into-memory.md).

# And now welcome to the segment I like to call "What could possibly go wrong?"

Before we start looking at what's actually _in_ those sections, let's make the example a little bit
more interesting.

```cpp
#include <unistd.h>

ssize_t greet() {
    static const char* greeting = "Hello world.\n";
    static const size_t greeting_length = 13;

    return write(STDOUT_FILENO, greeting, greeting_length);
}

int main() {
    return greet();
}
```

```sh
$ g++ -c greet.cpp -o greet.o
$ g++ greet.o -o greet
```

Let's remind ourselves of what sections are in the resulting `greet.o` and `greet` ELF files

```sh
$ readelf --wide --sections greet.o
There are 16 section headers, starting at offset 0x3a0:

Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        0000000000000000 000040 000032 00  AX  0   0  1
  [ 2] .rela.text        RELA            0000000000000000 000280 000048 18   I 13   1  8
  [ 3] .data             PROGBITS        0000000000000000 000072 000000 00  WA  0   0  1
  [ 4] .bss              NOBITS          0000000000000000 000072 000000 00  WA  0   0  1
  [ 5] .rodata           PROGBITS        0000000000000000 000078 000018 00   A  0   0  8
  [ 6] .data.rel.local   PROGBITS        0000000000000000 000090 000008 00  WA  0   0  8
  [ 7] .rela.data.rel.local RELA         0000000000000000 0002c8 000018 18   I 13   6  8
  [ 8] .comment          PROGBITS        0000000000000000 000098 000027 01  MS  0   0  1
  [ 9] .note.GNU-stack   PROGBITS        0000000000000000 0000bf 000000 00      0   0  1
  [10] .note.gnu.property NOTE           0000000000000000 0000c0 000020 00   A  0   0  8
  [11] .eh_frame         PROGBITS        0000000000000000 0000e0 000058 00   A  0   0  8
  [12] .rela.eh_frame    RELA            0000000000000000 0002e0 000030 18   I 13  11  8
  [13] .symtab           SYMTAB          0000000000000000 000138 0000f0 18     14   7  8
  [14] .strtab           STRTAB          0000000000000000 000228 000052 00      0   0  1
  [15] .shstrtab         STRTAB          0000000000000000 000310 000089 00      0   0  1
$ readelf --wide --sections greet
There are 31 section headers, starting at offset 0x3720:

Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .interp           PROGBITS        0000000000000318 000318 00001c 00   A  0   0  1
  [ 2] .note.gnu.property NOTE           0000000000000338 000338 000030 00   A  0   0  8
  [ 3] .note.gnu.build-id NOTE           0000000000000368 000368 000024 00   A  0   0  4
  [ 4] .note.ABI-tag     NOTE            000000000000038c 00038c 000020 00   A  0   0  4
  [ 5] .gnu.hash         GNU_HASH        00000000000003b0 0003b0 000024 00   A  6   0  8
  [ 6] .dynsym           DYNSYM          00000000000003d8 0003d8 0000a8 18   A  7   1  8
  [ 7] .dynstr           STRTAB          0000000000000480 000480 00008e 00   A  0   0  1
  [ 8] .gnu.version      VERSYM          000000000000050e 00050e 00000e 02   A  6   0  2
  [ 9] .gnu.version_r    VERNEED         0000000000000520 000520 000030 00   A  7   1  8
  [10] .rela.dyn         RELA            0000000000000550 000550 0000d8 18   A  6   0  8
  [11] .rela.plt         RELA            0000000000000628 000628 000018 18  AI  6  24  8
  [12] .init             PROGBITS        0000000000001000 001000 00001b 00  AX  0   0  4
  [13] .plt              PROGBITS        0000000000001020 001020 000020 10  AX  0   0 16
  [14] .plt.got          PROGBITS        0000000000001040 001040 000010 10  AX  0   0 16
  [15] .plt.sec          PROGBITS        0000000000001050 001050 000010 10  AX  0   0 16
  [16] .text             PROGBITS        0000000000001060 001060 00011b 00  AX  0   0 16
  [17] .fini             PROGBITS        000000000000117c 00117c 00000d 00  AX  0   0  4
  [18] .rodata           PROGBITS        0000000000002000 002000 000020 00   A  0   0  8
  [19] .eh_frame_hdr     PROGBITS        0000000000002020 002020 00003c 00   A  0   0  4
  [20] .eh_frame         PROGBITS        0000000000002060 002060 0000cc 00   A  0   0  8
  [21] .init_array       INIT_ARRAY      0000000000003db8 002db8 000008 08  WA  0   0  8
  [22] .fini_array       FINI_ARRAY      0000000000003dc0 002dc0 000008 08  WA  0   0  8
  [23] .dynamic          DYNAMIC         0000000000003dc8 002dc8 0001f0 10  WA  7   0  8
  [24] .got              PROGBITS        0000000000003fb8 002fb8 000048 08  WA  0   0  8
  [25] .data             PROGBITS        0000000000004000 003000 000018 00  WA  0   0  8
  [26] .bss              NOBITS          0000000000004018 003018 000008 00  WA  0   0  1
  [27] .comment          PROGBITS        0000000000000000 003018 000026 01  MS  0   0  1
  [28] .symtab           SYMTAB          0000000000000000 003040 0003a8 18     29  20  8
  [29] .strtab           STRTAB          0000000000000000 0033e8 00021a 00      0   0  1
  [30] .shstrtab         STRTAB          0000000000000000 003602 00011a 00      0   0  1
```

## String table

Let's start with an easy section. There are two **String Tables:** `.strtab` and `.shstrtab`. We've
already looked at `.shstrtab`; it's the string names of each section (the section headers just
contain an index into this table).

Let's look inside.

```sh
$ readelf -x .strtab greet.o
Hex dump of section '.strtab':
  0x00000000 00677265 65742e63 7070005f 5a5a3567 .greet.cpp._ZZ5g
  0x00000010 72656574 76453867 72656574 696e6700 reetvE8greeting.
  0x00000020 5f5a5a35 67726565 74764531 35677265 _ZZ5greetvE15gre
  0x00000030 6574696e 675f6c65 6e677468 005f5a35 eting_length._Z5
  0x00000040 67726565 74760077 72697465 006d6169 greetv.write.mai
  0x00000050 6e00                                n.
```

Those... sure are strings. Let's look for a more meaningful way to output this section.

```sh
$ readelf --string-dump=.strtab greet.o

String dump of section '.strtab':
  [     1]  greet.cpp
  [     b]  _ZZ5greetvE8greeting
  [    20]  _ZZ5greetvE15greeting_length
  [    3d]  _Z5greetv
  [    47]  write
  [    4d]  main
```

Okay, now we're getting somewhere, this actually looks like a table! Comparing to the hexdump above,
we see that it starts with a NULL byte, and then contains a null-terminated string: `677265 65742e63 707000`.
This is the ASCII text `greet.cpp`.

Cool. So this is a bunch of null-terminated strings concatenated together. Presumably, other
sections refer to the string table using an index.

## Symbol table

Let's immediately jump to the **Symbol Table** then, both because it'll help show how the string
table works, and because the symbol table is a useful debugging tool.

Using our little hexdump, we don't really make much sense of the `.symtab` section.

```sh
$ readelf -x .symtab greet.o
Hex dump of section '.symtab':
  0x00000000 00000000 00000000 00000000 00000000 ................
  0x00000010 00000000 00000000 01000000 0400f1ff ................
  0x00000020 00000000 00000000 00000000 00000000 ................
  0x00000030 00000000 03000100 00000000 00000000 ................
  0x00000040 00000000 00000000 00000000 03000500 ................
  0x00000050 00000000 00000000 00000000 00000000 ................
  0x00000060 00000000 03000600 00000000 00000000 ................
  0x00000070 00000000 00000000 0b000000 01000600 ................
  0x00000080 00000000 00000000 08000000 00000000 ................
  0x00000090 20000000 01000500 10000000 00000000  ...............
  0x000000a0 08000000 00000000 3d000000 12000100 ........=.......
  0x000000b0 00000000 00000000 23000000 00000000 ........#.......
  0x000000c0 47000000 10000000 00000000 00000000 G...............
  0x000000d0 00000000 00000000 4d000000 12000100 ........M.......
  0x000000e0 23000000 00000000 0f000000 00000000 #...............
```

Well. That's even less useful than the hexdump of `.strtab`, which at least included data that we
could make sense of.

If we reach for a more powerful tool,

```sh
$ readelf --wide --symbols greet.o
Symbol table '.symtab' contains 10 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS greet.cpp
     2: 0000000000000000     0 SECTION LOCAL  DEFAULT    1 .text
     3: 0000000000000000     0 SECTION LOCAL  DEFAULT    5 .rodata
     4: 0000000000000000     0 SECTION LOCAL  DEFAULT    6 .data.rel.local
     5: 0000000000000000     8 OBJECT  LOCAL  DEFAULT    6 _ZZ5greetvE8greeting
     6: 0000000000000010     8 OBJECT  LOCAL  DEFAULT    5 _ZZ5greetvE15greeting_length
     7: 0000000000000000    35 FUNC    GLOBAL DEFAULT    1 _Z5greetv
     8: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND write
     9: 0000000000000023    15 FUNC    GLOBAL DEFAULT    1 main
```
we see that it presumably holds some strings, but those strings aren't in the symbol table! That's
because they're in the string table, where strings belong ;)

If we look at the symbol table definition in [elf(5)](https://linux.die.net/man/5/elf), we find (for
64-bit ELF files)
```c
typedef struct {
    uint32_t      st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
    Elf64_Addr    st_value;
    uint64_t      st_size;
} Elf64_Sym;
```
that each symbol entry is 24 bytes, and that `st_name` is an index into the `.strtab` table (for
regular symbols. For sections, it's an index into the `.shstrtab` table). Let's verify.

The first symbol, is all NULL-padding.
```sh
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
```
So let's look at the second symbol:
```sh
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS greet.cpp
```
That's going to be nearabouts
```sh
  0x00000000 00000000 00000000 00000000 00000000 ................
  0x00000010 00000000 00000000 01000000 0400f1ff ................
  0x00000020 00000000 00000000 00000000 00000000 ................
```
Let's narrow it down using the fact that `Elf64_Sym` is 24 bytes. Each grouping is four bytes, so
the first symbol is the first size groups of NULL bytes:
```sh
  0x00000000 00000000 00000000 00000000 00000000 ................
  0x00000010 00000000 00000000                   ........
```
and the second symbol is the next 6 groups:
```sh
  0x00000010                   01000000 0400f1ff         ........
  0x00000020 00000000 00000000 00000000 00000000 ................
```
And sure enough, the first 32 bits of the symbol (the `st_name`) is `0x01`, or rather, the second
string in the string table:

```sh
$ readelf --string-dump=.strtab greet.o
String dump of section '.strtab':
  [     1]  greet.cpp
  [     b]  _ZZ5greetvE8greeting
  [    20]  _ZZ5greetvE15greeting_length
  [    3d]  _Z5greetv
  [    47]  write
  [    4d]  main
```
(remember the first string is a single NULL byte, and that the numbers in the square brackets are
indices).

So now what is this  `_ZZ5greetvE15greeting_length` nonsense? It turns out that C++ (and other)
compilers **Mangle** symbol names. The compilers do this so that symbol names (think of overloaded
functions, and variable names!) are unique (to facilitate linking, which we're not going to talk
about).

There's a binutils tool called [c++filt(1)](https://linux.die.net/man/1/c++filt) that can be used to
demangle names. `readelf` and `objdump` also have `--demangle` flags, but `c++filt` works on
literally anything you feed it.

```sh
$ readelf --string-dump=.strtab greet.o | c++filt

String dump of section '.strtab':
  [     1]  greet.cpp
  [     b]  greet()::greeting
  [    20]  greet()::greeting_length
  [    3d]  greet()
  [    47]  write
  [    4d]  main
```

Knowing that ELF files (that haven't been stripped) contain symbols is a remarkably useful bit of
knowledge to have in your back pocket. I've used this information to search for libraries that
provide a symbol, and I've used it to troubleshoot both ODR (One Definition Rule) violations and
undefined symbol problems.

[nm(1)](https://linux.die.net/man/1/nm) is another binutils tool for listing symbols in an ELF file.
It differs from `readelf` and `objdump` in that the output format is less complicated and easier to
parse, and that it's intended purely for _just_ dumping the symbols, rather than being a Swiss army
knife.

```sh
$ nm --demangle greet.o
0000000000000023 T main
                 U write
0000000000000000 T greet()
0000000000000010 r greet()::greeting_length
0000000000000000 d greet()::greeting
```

## Stripping

Notice from above that we did _not_ build the `./greet` example with debug symbols.
Using [file(1)](https://linux.die.net/man/1/file) confirms this:
```sh
$ file greet.o greet
greet.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
greet:   ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=5d2078ddcc7d7cd4770bb5c7a833c02f83f6dbf4, for GNU/Linux 3.2.0, not stripped
```

But it _does_ say that the ELF files are **not stripped**. Let's use
[strip(1)](https://linux.die.net/man/1/strip) to _strip_ the unneeded symbols, and see what happens.

Before:
```sh
$ readelf --demangle --wide --symbols greet

Symbol table '.dynsym' contains 7 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __libc_start_main@GLIBC_2.34 (2)
     2: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_deregisterTMCloneTable
     3: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND write@GLIBC_2.2.5 (3)
     4: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND __gmon_start__
     5: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_registerTMCloneTable
     6: 0000000000000000     0 FUNC    WEAK   DEFAULT  UND __cxa_finalize@GLIBC_2.2.5 (3)

Symbol table '.symtab' contains 39 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS Scrt1.o
     2: 000000000000038c    32 OBJECT  LOCAL  DEFAULT    4 __abi_tag
     3: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS crtstuff.c
     4: 0000000000001090     0 FUNC    LOCAL  DEFAULT   16 deregister_tm_clones
     5: 00000000000010c0     0 FUNC    LOCAL  DEFAULT   16 register_tm_clones
     6: 0000000000001100     0 FUNC    LOCAL  DEFAULT   16 __do_global_dtors_aux
     7: 0000000000004018     1 OBJECT  LOCAL  DEFAULT   26 completed.0
     8: 0000000000003dc0     0 OBJECT  LOCAL  DEFAULT   22 __do_global_dtors_aux_fini_array_entry
     9: 0000000000001140     0 FUNC    LOCAL  DEFAULT   16 frame_dummy
    10: 0000000000003db8     0 OBJECT  LOCAL  DEFAULT   21 __frame_dummy_init_array_entry
    11: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS greet.cpp
    12: 0000000000004010     8 OBJECT  LOCAL  DEFAULT   25 greet()::greeting
    13: 0000000000002018     8 OBJECT  LOCAL  DEFAULT   18 greet()::greeting_length
    14: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS crtstuff.c
    15: 0000000000002128     0 OBJECT  LOCAL  DEFAULT   20 __FRAME_END__
    16: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS
    17: 0000000000003dc8     0 OBJECT  LOCAL  DEFAULT   23 _DYNAMIC
    18: 0000000000002020     0 NOTYPE  LOCAL  DEFAULT   19 __GNU_EH_FRAME_HDR
    19: 0000000000003fb8     0 OBJECT  LOCAL  DEFAULT   24 _GLOBAL_OFFSET_TABLE_
    20: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __libc_start_main@GLIBC_2.34
    21: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_deregisterTMCloneTable
    22: 0000000000004000     0 NOTYPE  WEAK   DEFAULT   25 data_start
    23: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND write@GLIBC_2.2.5
    24: 0000000000004018     0 NOTYPE  GLOBAL DEFAULT   25 _edata
    25: 000000000000117c     0 FUNC    GLOBAL HIDDEN    17 _fini
    26: 0000000000004000     0 NOTYPE  GLOBAL DEFAULT   25 __data_start
    27: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND __gmon_start__
    28: 0000000000004008     0 OBJECT  GLOBAL HIDDEN    25 __dso_handle
    29: 0000000000002000     4 OBJECT  GLOBAL DEFAULT   18 _IO_stdin_used
    30: 0000000000004020     0 NOTYPE  GLOBAL DEFAULT   26 _end
    31: 0000000000001060    38 FUNC    GLOBAL DEFAULT   16 _start
    32: 0000000000004018     0 NOTYPE  GLOBAL DEFAULT   26 __bss_start
    33: 000000000000116c    15 FUNC    GLOBAL DEFAULT   16 main
    34: 0000000000001149    35 FUNC    GLOBAL DEFAULT   16 greet()
    35: 0000000000004018     0 OBJECT  GLOBAL HIDDEN    25 __TMC_END__
    36: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_registerTMCloneTable
    37: 0000000000000000     0 FUNC    WEAK   DEFAULT  UND __cxa_finalize@GLIBC_2.2.5
    38: 0000000000001000     0 FUNC    GLOBAL HIDDEN    12 _init
```

After:
```sh
$ wc -c greet
16096 greet
$ strip --strip-unneeded greet
$ readelf --demangle --wide --symbols greet
Symbol table '.dynsym' contains 7 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __libc_start_main@GLIBC_2.34 (2)
     2: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_deregisterTMCloneTable
     3: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND write@GLIBC_2.2.5 (3)
     4: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND __gmon_start__
     5: 0000000000000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_registerTMCloneTable
     6: 0000000000000000     0 FUNC    WEAK   DEFAULT  UND __cxa_finalize@GLIBC_2.2.5 (3)
$ wc -c greet
14472 greet
```

That pruned almost 2KB off of the executable.
And now `file` tells us it's stripped:
```sh
$ file greet
greet: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=5d2078ddcc7d7cd4770bb5c7a833c02f83f6dbf4, for GNU/Linux 3.2.0, stripped
```

`strip` removed the entire `.symtab` section, but still left the `.dynsym` section. That's because,
in the implementation details of dynamic symbol resolution, which we won't talk about here, the
dynamic loader [ld.so(8)](https://linux.die.net/man/8/ld.so) needs the dynamic symbol name in order
to resolve it at run time.

## Code

We'll talk more about the `.text` section of `greet.o` below. For now though, let's just look at the
assembled code for the final `greet` executable. But before we do that, let's _un_-strip the `greet`
executable by rebuilding it. Let's also add debug symbols to make digging in with GDB a bit easier.

```sh
$ g++ -ggdb3 greet.cpp -o greet
```

Now, when we look at the disassembly, we should see some more useful output.

```sh
$ objdump --demangle --disassemble -M intel --section=.text greet
Disassembly of section .text:

0000000000001060 <_start>:
    1060:	f3 0f 1e fa          	endbr64
    1064:	31 ed                	xor    ebp,ebp
    1066:	49 89 d1             	mov    r9,rdx
    1069:	5e                   	pop    rsi
    106a:	48 89 e2             	mov    rdx,rsp
    106d:	48 83 e4 f0          	and    rsp,0xfffffffffffffff0
    1071:	50                   	push   rax
    1072:	54                   	push   rsp
    1073:	45 31 c0             	xor    r8d,r8d
    1076:	31 c9                	xor    ecx,ecx
    1078:	48 8d 3d ed 00 00 00 	lea    rdi,[rip+0xed]                # 0x116c <main>
    107f:	ff 15 53 2f 00 00    	call   QWORD PTR [rip+0x2f53]        # 0x3fd8 <__libc_start_main@GLIBC_2.34>
    1085:	f4                   	hlt
    1086:	66 2e 0f 1f 84 00 00 	cs nop WORD PTR [rax+rax*1+0x0]
    108d:	00 00 00

0000000000001149 <greet()>:
    1149:	f3 0f 1e fa          	endbr64
    114d:	55                   	push   rbp
    114e:	48 89 e5             	mov    rbp,rsp
    1151:	48 8b 05 b8 2e 00 00 	mov    rax,QWORD PTR [rip+0x2eb8]    # 0x4010 <greet()::greeting>
    1158:	ba 0d 00 00 00       	mov    edx,0xd
    115d:	48 89 c6             	mov    rsi,rax
    1160:	bf 01 00 00 00       	mov    edi,0x1
    1165:	e8 e6 fe ff ff       	call   1050 <write@plt>
    116a:	5d                   	pop    rbp
    116b:	c3                   	ret

000000000000116c <main>:
    116c:	f3 0f 1e fa          	endbr64
    1170:	55                   	push   rbp
    1171:	48 89 e5             	mov    rbp,rsp
    1174:	e8 d0 ff ff ff       	call   1149 <greet()>
    1179:	5d                   	pop    rbp
    117a:	c3                   	ret
```

## Data

### Static constants
If we refer back to our example,
```cpp
#include <unistd.h>

ssize_t greet() {
    static const char* greeting = "Hello world.\n";
    static const size_t greeting_length = 13;

    return write(STDOUT_FILENO, greeting, greeting_length);
}

int main() {
    return greet();
}
```
the `greet()::greeting` symbol is a static string constant. Based on my experience, I'd expect this
to be in the `.rodata` (read only data) section, which gets loaded to a read-only memory page.

Inspecting the `.rodata` section with `readelf` confirms:

```sh
$ readelf -x .rodata greet

Hex dump of section '.rodata':
  0x00002000 01000200 00000000 48656c6c 6f20776f ........Hello wo
  0x00002010 726c642e 0a000000 0d000000 00000000 rld.............
```

So the string `Hello world\n` is stored at address `0x2008`.

But in the disassembly above, `objdump` claims that the symbol lives at `0x4010`?! What's going on?
```sh
    1151:	48 8b 05 b8 2e 00 00 	mov    rax,QWORD PTR [rip+0x2eb8]    # 0x4010 <greet()::greeting>
```

(disclaimer: I don't speak assembly, and I _definitely_ don't speak Intel style x86_64 assembly, so
someone who knows what `QWORD PTR` does should probably skip ahead)

That's taking the instruction pointer, which is conveniently listed on the left: `0x1151`, and add
`0x2eb8`, we get `0x4009`, which _almost_ agrees with the listed `0x4010` address that `objdump`
claims refers to the `greet()::greeting` symbol. I don't know why it's off by one, but I'm going to
pretend that our math came out right, and we get `0x4010`.

But the string constant starts at address `0x2008`, not `0x4010`, so what's going on!? If we look at
address `0x4010`, (which I happen to know is in the `.data` section) we find something interesting!!

```sh
$ readelf -x .data greet

Hex dump of section '.data':
  0x00004000 00000000 00000000 08400000 00000000 .........@......
  0x00004010 08200000 00000000                   . ......
```

At address `0x4010`, there's a QWORD that is `0x2008`!!

So there's a double indirection: We take `$rip+0x2eb8` and get `0x4010`, dereference, and find
`0x2008`, which we then load into `$rax` before the `write(2)` syscall. It turns out this isn't
surprising if you can read Intel-style assembly, and know that `QWORD PTR` treats the RHS as an
address that needs to be dereferenced.

Mystery solved. But let's step through in GDB just to make sure I haven't invented any more lies
than usual.

```sh
$ gdb -q -ex "layout split" -ex "focus cmd" -ex "break greet" -ex "run" greet
(gdb) print $rip
$1 = (void (*)(void)) 0x555555555151 <greet()+8>
```

This shows that the instruction pointer is pointing to the `0x1151` instruction (remember that the
addresses in the ELF files are relative to the base address, which in this case is
`0x555555554000`), or rather, the
```asm
mov     rax,QWORD PTR [rip+0x2eb8]      # 0x4010 <greet()::greeting>
```
instruction.

So let's do the math, and then step to the next instruction and double check.

```sh
(gdb) print/x $rip + 0x2eb8
$2 = 0x555555558009
```

again, we get an address that ends in `0x9`, not the expected `0x10`. I think this is because
`[rip+0x2eb8]` might take the value of `$rip` _after_ it's been incremented to the next
instruction?, and I'm just unfamiliar? Anyways, the next instruction is `0x07` bytes later, so let's
add another 7 bytes

```sh
(gdb) print/x $rip + 0x2eb8 + 0x07
$3 = 0x555555558010
```

We can double check our belief that this is in the `.data` section, not the `.rodata` section:

```sh
(gdb) maintenance info sections .data .rodata
Exec file: `/home/nots/Documents/elf-interrogation/examples/greet/greet', file type elf64-x86-64.
 [17]     0x555555556000->0x555555556020 at 0x00002000: .rodata ALLOC LOAD READONLY DATA HAS_CONTENTS
 [24]     0x555555558000->0x555555558018 at 0x00003000: .data ALLOC LOAD DATA HAS_CONTENTS
```

Yup. `0x555555558010` is in the `.data` section range.
And it's also the expected value too; if we look at what's there, we'll find another pointer:

```sh
(gdb) print/x *(intptr_t*) ($rip + 0x2eb8 + 0x07)
$4 = 0x555555556008
```

And this pointer is even in the `.rodata` section!!

and if we look _here_, we should find the `"Hello world\n"` string.

```sh
(gdb) x/s 0x555555556008
0x555555556008: "Hello world.\n"
```

Ding! Ding! Ding!

Now that we've done the math, let's step to the next instruction. We expect `$rax` to hold
`0x555555556008`, if our understanding is correct.

```sh
(gdb) stepi
(gdb) p/x $rax
$5 = 0x555555556008
```

Cool. Learning how static constants work was fun.

### Local constants
### Globals / static locals
## Relocations
## Dynamic symbols
## DT_RPATH and DT_BIND_NOW?
## Metadata
How do kernel modules add metadata for author, etc.?
http://www.bottomupcs.com/elf-sections-others.xhtml
## Debug symbols

```sh
$ llvm-dwarfdump-14 --name=greeting ./greet
greet:	file format elf64-x86-64

0x0000010a: DW_TAG_variable
              DW_AT_name        ("greeting")
              DW_AT_decl_file	("/home/nots/Documents/elf-interrogation/examples/greet/greet.cpp")
              DW_AT_decl_line	(4)
              DW_AT_decl_column	(0x18)
              DW_AT_type	    (0x00000135 "const char *")
              DW_AT_location	(DW_OP_addr 0x4010)
```

If you use `readelf`, this looks like
```sh
$ readelf --debug-dump=info ./greet
...
 <2><10a>: Abbrev Number: 6 (DW_TAG_variable)
    <10b>   DW_AT_name        : (indirect string, offset: 0x2f2c): greeting
    <10f>   DW_AT_decl_file   : 1
    <10f>   DW_AT_decl_line   : 4
    <110>   DW_AT_decl_column : 24
    <111>   DW_AT_type        : <0x135>
    <115>   DW_AT_location    : 9 byte block: 3 10 40 0 0 0 0 0 0 	(DW_OP_addr: 4010)
...
```
## How to customize sections with GCC?
