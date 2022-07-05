# How are Linux processes loaded into memory?

# Contents

- [How are Linux processes loaded into memory?](#how-are-linux-processes-loaded-into-memory)
- [Program headers and segments](#program-headers-and-segments)
- [The stack](#the-stack)
- [The heap](#the-heap)
- [ASLR](#aslr)
- [Summary](#summary)

# Program headers and segments

Recall from [What's an ELF file?](./whats-an-elf-file.md) that there's a **Program Header** in some
kinds of ELF files (the loadable kind) that defines what **Segments** are loaded into memory.
Segments, and themselves, one or more **Sections** concatenated together.

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

So let's run this application with GDB and start looking at where things are in memory.

```sh
$ gdb --quiet -ex "break main" -ex "run" empty
(gdb) info inferior
  Num  Description       Connection           Executable
* 1    process 230427    1 (native)           /home/nots/Documents/elf-interrogation/examples/empty/empty
```

In another terminal (so we can keep the process running) let's poke and prod at PID `230427`

```sh

$ cat /proc/230427/maps
555555554000-555555555000 r--p 00000000 103:05 21891083                  /home/nots/Documents/elf-interrogation/examples/empty/empty
555555555000-555555556000 r-xp 00001000 103:05 21891083                  /home/nots/Documents/elf-interrogation/examples/empty/empty
555555556000-555555557000 r--p 00002000 103:05 21891083                  /home/nots/Documents/elf-interrogation/examples/empty/empty
555555557000-555555558000 r--p 00002000 103:05 21891083                  /home/nots/Documents/elf-interrogation/examples/empty/empty
555555558000-555555559000 rw-p 00003000 103:05 21891083                  /home/nots/Documents/elf-interrogation/examples/empty/empty
7ffff7d77000-7ffff7d7a000 rw-p 00000000 00:00 0
7ffff7d7a000-7ffff7da2000 r--p 00000000 103:05 9835249                   /usr/lib/x86_64-linux-gnu/libc.so.6
7ffff7da2000-7ffff7f37000 r-xp 00028000 103:05 9835249                   /usr/lib/x86_64-linux-gnu/libc.so.6
7ffff7f37000-7ffff7f8f000 r--p 001bd000 103:05 9835249                   /usr/lib/x86_64-linux-gnu/libc.so.6
7ffff7f8f000-7ffff7f93000 r--p 00214000 103:05 9835249                   /usr/lib/x86_64-linux-gnu/libc.so.6
7ffff7f93000-7ffff7f95000 rw-p 00218000 103:05 9835249                   /usr/lib/x86_64-linux-gnu/libc.so.6
7ffff7f95000-7ffff7fa2000 rw-p 00000000 00:00 0
7ffff7fbb000-7ffff7fbd000 rw-p 00000000 00:00 0
7ffff7fbd000-7ffff7fc1000 r--p 00000000 00:00 0                          [vvar]
7ffff7fc1000-7ffff7fc3000 r-xp 00000000 00:00 0                          [vdso]
7ffff7fc3000-7ffff7fc5000 r--p 00000000 103:05 9834696                   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7ffff7fc5000-7ffff7fef000 r-xp 00002000 103:05 9834696                   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7ffff7fef000-7ffff7ffa000 r--p 0002c000 103:05 9834696                   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7ffff7ffb000-7ffff7ffd000 r--p 00037000 103:05 9834696                   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7ffff7ffd000-7ffff7fff000 rw-p 00039000 103:05 9834696                   /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7ffffffde000-7ffffffff000 rw-p 00000000 00:00 0                          [stack]
ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0                  [vsyscall]
```

This data comes from the [proc(5)](https://linux.die.net/man/5/proc) virtual filesystem provided by the Linux kernel. GDB
also provides its own view for this data:

```sh
(gdb) info proc mappings
process 230427
Mapped address spaces:

          Start Addr           End Addr       Size     Offset  Perms  objfile
      0x555555554000     0x555555555000     0x1000        0x0  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555555000     0x555555556000     0x1000     0x1000  r-xp   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555556000     0x555555557000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555557000     0x555555558000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/empty/empty
      0x555555558000     0x555555559000     0x1000     0x3000  rw-p   /home/nots/Documents/elf-interrogation/examples/empty/empty
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

We want more detail.

```sh
(gdb) maintenance info sections
Exec file: '/home/nots/Documents/elf-interrogation/examples/empty/empty', file type elf64-x86-64.
 [0]      0x555555554318->0x555555554334 at 0x00000318: .interp ALLOC LOAD READONLY DATA HAS_CONTENTS
 [1]      0x555555554338->0x555555554368 at 0x00000338: .note.gnu.property ALLOC LOAD READONLY DATA HAS_CONTENTS
 [2]      0x555555554368->0x55555555438c at 0x00000368: .note.gnu.build-id ALLOC LOAD READONLY DATA HAS_CONTENTS
 [3]      0x55555555438c->0x5555555543ac at 0x0000038c: .note.ABI-tag ALLOC LOAD READONLY DATA HAS_CONTENTS
 [4]      0x5555555543b0->0x5555555543d4 at 0x000003b0: .gnu.hash ALLOC LOAD READONLY DATA HAS_CONTENTS
 [5]      0x5555555543d8->0x555555554468 at 0x000003d8: .dynsym ALLOC LOAD READONLY DATA HAS_CONTENTS
 [6]      0x555555554468->0x5555555544f0 at 0x00000468: .dynstr ALLOC LOAD READONLY DATA HAS_CONTENTS
 [7]      0x5555555544f0->0x5555555544fc at 0x000004f0: .gnu.version ALLOC LOAD READONLY DATA HAS_CONTENTS
 [8]      0x555555554500->0x555555554530 at 0x00000500: .gnu.version_r ALLOC LOAD READONLY DATA HAS_CONTENTS
 [9]      0x555555554530->0x5555555545f0 at 0x00000530: .rela.dyn ALLOC LOAD READONLY DATA HAS_CONTENTS
 [10]     0x555555555000->0x55555555501b at 0x00001000: .init ALLOC LOAD READONLY CODE HAS_CONTENTS
 [11]     0x555555555020->0x555555555030 at 0x00001020: .plt ALLOC LOAD READONLY CODE HAS_CONTENTS
 [12]     0x555555555030->0x555555555040 at 0x00001030: .plt.got ALLOC LOAD READONLY CODE HAS_CONTENTS
 [13]     0x555555555040->0x555555555138 at 0x00001040: .text ALLOC LOAD READONLY CODE HAS_CONTENTS
 [14]     0x555555555138->0x555555555145 at 0x00001138: .fini ALLOC LOAD READONLY CODE HAS_CONTENTS
 [15]     0x555555556000->0x555555556004 at 0x00002000: .rodata ALLOC LOAD READONLY DATA HAS_CONTENTS
 [16]     0x555555556004->0x555555556030 at 0x00002004: .eh_frame_hdr ALLOC LOAD READONLY DATA HAS_CONTENTS
 [17]     0x555555556030->0x5555555560c4 at 0x00002030: .eh_frame ALLOC LOAD READONLY DATA HAS_CONTENTS
 [18]     0x555555557df0->0x555555557df8 at 0x00002df0: .init_array ALLOC LOAD DATA HAS_CONTENTS
 [19]     0x555555557df8->0x555555557e00 at 0x00002df8: .fini_array ALLOC LOAD DATA HAS_CONTENTS
 [20]     0x555555557e00->0x555555557fc0 at 0x00002e00: .dynamic ALLOC LOAD DATA HAS_CONTENTS
 [21]     0x555555557fc0->0x555555558000 at 0x00002fc0: .got ALLOC LOAD DATA HAS_CONTENTS
 [22]     0x555555558000->0x555555558010 at 0x00003000: .data ALLOC LOAD DATA HAS_CONTENTS
 [23]     0x555555558010->0x555555558018 at 0x00003010: .bss ALLOC
 [24]     0x00000000->0x00000026 at 0x00003010: .comment READONLY HAS_CONTENTS
```

this shows us where each section is loaded (in higher fidelity than just segments)! We can also run
`info file` for sections loaded from the various DSOs loaded by the dynamic loader (which we won't
talk about).

# The stack

From the `info proc mappings` above, we can see where the stack is:

> ```
> Start Addr         End Addr          Size        Offset  Perms  objfile
> 0x7ffffffde000     0x7ffffffff000    0x21000        0x0  rw-p   [stack]
> ```

We can use `info frame` to get more information about the _current_ stack frame

```sh
(gdb) info frame
Stack level 0, frame at 0x7fffffffdef0:
 rip = 0x555555555131 in main; saved rip = 0x7ffff7da3d90
 Arglist at 0x7fffffffdee0, args:
 Locals at 0x7fffffffdee0, Previous frame's sp is 0x7fffffffdef0
 Saved registers:
  rbp at 0x7fffffffdee0, rip at 0x7fffffffdee8
```
from which we can see that the stack is indeed in the range `0x7ffffffde000` through `0x7ffffffff000`.

# The heap

Let's add something to the heap so we can get a sense for where the heap is at in memory, relative
to the rest of the process.

```cpp
#include <stdlib.h>
#include <unistd.h>

int main() {
    static const size_t element_size = 1; // 1 byte per element
    static const size_t elements = 10;    // 10 elements

    // Bracket the heap operations in write(2) syscalls so we can trace with strace(1) easier
    write(STDOUT_FILENO, "Before calloc\n", 14);
    void *heap_buffer = calloc(elements, element_size);
    write(STDOUT_FILENO, "After calloc\n", 13);
    free(heap_buffer);
    write(STDOUT_FILENO, "After free\n", 11);
    return 0;
}
```

But the heap doesn't exist until the first allocation is made, so we need to build with debug
symbols in order to set meaningful breakpoints.

```sh
$ g++ -g heap.cpp -o heap
$ gdb -quiet heap
Reading symbols from heap...
(gdb) break heap.cpp:10
Breakpoint 1 at 0x11ae: file heap.cpp, line 10.
(gdb) run
Starting program: /home/nots/Documents/elf-interrogation/examples/heap/heap
Breakpoint 1, main () at heap.cpp:10
10	    void *heap_buffer = calloc(elements, element_size);
```

Now we're broken immediately before the first heap allocation. So we look at the `proc(5)` memory
mappings:

```sh
(gdb) info proc mappings
process 255889
Mapped address spaces:

          Start Addr           End Addr       Size     Offset  Perms  objfile
      0x555555554000     0x555555555000     0x1000        0x0  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x555555555000     0x555555556000     0x1000     0x1000  r-xp   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x555555556000     0x555555557000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x555555557000     0x555555558000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x555555558000     0x555555559000     0x1000     0x3000  rw-p   /home/nots/Documents/elf-interrogation/examples/heap/heap
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

This looks _very_ similar to the `empty` example above. There's a `[stack]`, but no `[heap]`!
So we step over the `calloc(3)` call, and inspect the mappings again:

```sh
(gdb) next
11	    write(STDOUT_FILENO, "After calloc\n", 13);
(gdb) info proc mappings
...
          Start Addr           End Addr       Size     Offset  Perms  objfile
...
      0x555555559000     0x55555557a000    0x21000        0x0  rw-p   [heap]
(program break)
...
      0x7ffffffde000     0x7ffffffff000    0x21000        0x0  rw-p   [stack]
```

So to summarize, the process memory footprint looks like

```
+--------------+ 0x555555554000
|              |
| ELF Segments |
|              |
+--------------+ 0x555555559000
|              |
| Heap         |
|              |
+--------------+ 0x55555557a000 (program break)
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

Both the stack and the heap will grow "downwards".

# ASLR

Linux, by default, performs what's called Address Space Layout Randomization as a way to mitigate
certain kinds of vulnerabilities where malicious code gets loaded dynamically and uses predictable
offsets to overwrite (or just reason about) your application's code. So, by default, your process
(and any loaded DSOs) gets loaded to a different base address every time it's executed.

GDB disables ASLR by default to facilitate sane debugging. But let's enable it just to see:

```sh
$ gdb -quiet \
    -ex "set disable-randomization off" \
    -ex "break heap.cpp:8" \
    -ex "run" \
    -ex "info proc mappings" \
    -batch heap | grep "Start Addr" -A 4
          Start Addr           End Addr       Size     Offset  Perms  objfile
      0x55a43b8a8000     0x55a43b8a9000     0x1000        0x0  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x55a43b8a9000     0x55a43b8aa000     0x1000     0x1000  r-xp   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x55a43b8aa000     0x55a43b8ab000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x55a43b8ab000     0x55a43b8ac000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
$ gdb -quiet \
    -ex "set disable-randomization off" \
    -ex "break heap.cpp:8" \
    -ex "run" \
    -ex "info proc mappings" \
    -batch heap | grep "Start Addr" -A 4
          Start Addr           End Addr       Size     Offset  Perms  objfile
      0x56427831f000     0x564278320000     0x1000        0x0  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x564278320000     0x564278321000     0x1000     0x1000  r-xp   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x564278321000     0x564278322000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
      0x564278322000     0x564278323000     0x1000     0x2000  r--p   /home/nots/Documents/elf-interrogation/examples/heap/heap
```

It's important to note that these are virtual addresses, and that the physical addresses are
virtualized for each process. That is, you can have multiple running processes each with a base
address of `0x555555554000` -- processes can't see other processes memory.

Finally, let's watch [ld.so(8)](https://linux.die.net/man/8/ld.so) load our process into memory
using [strace(1)](https://linux.die.net/man/1/strace).

```sh
$ echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
$ strace --successful-only /lib64/ld-linux-x86-64.so.2 ./heap
execve("/lib64/ld-linux-x86-64.so.2", ["/lib64/ld-linux-x86-64.so.2", "./heap"], 0x7fffffffe040 /* 87 vars */) = 0
brk(NULL)                               = 0x7ffff7fff000
openat(AT_FDCWD, "./heap", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\240\20\0\0\0\0\0\0"..., 832) = 832
pread64(3, "\4\0\0\0 \0\0\0\5\0\0\0GNU\0\2\0\0\300\4\0\0\0\3\0\0\0\0\0\0\0"..., 48, 824) = 48
pread64(3, "\4\0\0\0\24\0\0\0\3\0\0\0GNU\0\304y\214\207\216'\7\346\217\345&\210Z\246\362\267"..., 68, 872) = 68
getcwd("/home/nots/Documents/elf-interrogation/examples/heap", 128) = 53
mmap(NULL, 16408, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7ffff7fb8000
mmap(0x7ffff7fb9000, 4096, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1000) = 0x7ffff7fb9000
mmap(0x7ffff7fba000, 4096, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7ffff7fba000
mmap(0x7ffff7fbb000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7ffff7fbb000
close(3)                                = 0
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7ffff7fb6000
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
newfstatat(3, "", {st_mode=S_IFREG|0644, st_size=101065, ...}, AT_EMPTY_PATH) = 0
mmap(NULL, 101065, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7ffff7f9d000
close(3)                                = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0P\237\2\0\0\0\0\0"..., 832) = 832
pread64(3, "\6\0\0\0\4\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0"..., 784, 64) = 784
pread64(3, "\4\0\0\0 \0\0\0\5\0\0\0GNU\0\2\0\0\300\4\0\0\0\3\0\0\0\0\0\0\0"..., 48, 848) = 48
pread64(3, "\4\0\0\0\24\0\0\0\3\0\0\0GNU\0\211\303\313\205\371\345PFwdq\376\320^\304A"..., 68, 896) = 68
newfstatat(3, "", {st_mode=S_IFREG|0644, st_size=2216304, ...}, AT_EMPTY_PATH) = 0
pread64(3, "\6\0\0\0\4\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0"..., 784, 64) = 784
mmap(NULL, 2260560, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7ffff7d75000
mmap(0x7ffff7d9d000, 1658880, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x28000) = 0x7ffff7d9d000
mmap(0x7ffff7f32000, 360448, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1bd000) = 0x7ffff7f32000
mmap(0x7ffff7f8a000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x214000) = 0x7ffff7f8a000
mmap(0x7ffff7f90000, 52816, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) = 0x7ffff7f90000
close(3)                                = 0
mmap(NULL, 12288, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7ffff7d72000
arch_prctl(ARCH_SET_FS, 0x7ffff7d72740) = 0
set_tid_address(0x7ffff7d72a10)         = 320672
set_robust_list(0x7ffff7d72a20, 24)     = 0
rseq(0x7ffff7d730e0, 0x20, 0, 0x53053053) = 0
mprotect(0x7ffff7f8a000, 16384, PROT_READ) = 0
mprotect(0x7ffff7fbb000, 4096, PROT_READ) = 0
mprotect(0x7ffff7ffb000, 8192, PROT_READ) = 0
prlimit64(0, RLIMIT_STACK, NULL, {rlim_cur=8192*1024, rlim_max=RLIM64_INFINITY}) = 0
munmap(0x7ffff7f9d000, 101065)          = 0
write(1, "Before calloc\n", 14)         = 14
getrandom("\xe4\x33\x9f\x75\x68\x0c\x6b\x36", 8, GRND_NONBLOCK) = 8
brk(NULL)                               = 0x7ffff7fff000
brk(0x7ffff8020000)                     = 0x7ffff8020000
write(1, "After calloc\n", 13)          = 13
write(1, "After free\n", 11)            = 11
+++ exited with 0 +++
```

This shows the dynamic loader (the thing that actually loads your program and executes it) reading
chunks from the `./heap` file into buffers:

```c
openat(AT_FDCWD, "./heap", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\200\20\0\0\0\0\0\0"..., 832) = 832
pread64(3, "\4\0\0\0 \0\0\0\5\0\0\0GNU\0\2\0\0\300\4\0\0\0\3\0\0\0\0\0\0\0"..., 48, 824) = 48
pread64(3, "\4\0\0\0\24\0\0\0\3\0\0\0GNU\0\v\270\272\242\333\17\331d\314G\r\365h\230\375\323"..., 68, 872) = 68
```

and then map `0x1000` size segments of the file into various regions of memory with different
permissions:

```c
// the first 16408 bytes of the file loaded at the front of the process memory space
// The address is NULL, so we're asking the kernel to pick an address to map the file to.
mmap(NULL, 16408, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7ffff7fb8000
mmap(0x7ffff7fb9000, 4096, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1000) = 0x7ffff7fb9000
mmap(0x7ffff7fba000, 4096, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7ffff7fba000
mmap(0x7ffff7fbb000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7ffff7fbb000
close(3) = 0
```

However, note that the pointers shown are from the perspective of the `ld-linux.so` process, _not_
the `./heap` process!

# Summary

So that's a little bit about the general format of an ELF file. There's headers, sections, and
segments. Segments are what get loaded into memory, and they consist of one or more sections.

```
+--------------+ 0x555555554000
|              |
| ELF Segments |
|              |
+--------------+ 0x555555559000
|              |
| Heap         |
|              |
+--------------+ 0x55555557a000 (program break)
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

