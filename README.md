# ELF Interrogation
Material for a presentation on ELF file interrogation inspired by Matt Godbolt's cppcon talk "The bits between the bits"

## Why does it matter?

It's fascinating! And also useful!

Deepening my understanding of
* The compilation process
* The binary compilation artifacts
* How processes start (specifically w.r.t. dynamic symbol resolution)

has increased my confidence, and enabled me to answer more challenging questions with less
information available. I've been able to figure out tricky linker issues, debug DSO search paths,
troubleshoot static global initialization ordering, troubleshoot versioned symbol conflicts, debug
crashes on process startup, etc.

## Interrogation tools

There are a _host_ of [binutils](https://www.gnu.org/software/binutils/),
[elfutils](https://sourceware.org/elfutils/), [llvm-tools](https://llvm.org/docs/CommandGuide/) and
other tools for interrogating ELF files. I've used (or otherwise poked at) the following:

* [c++filt(1)](https://linux.die.net/man/1/c++filt) (and
  [rustfilt](https://github.com/luser/rustfilt))
* [addr2line(1)](https://linux.die.net/man/1/addr2line)
* [readelf(1)](https://linux.die.net/man/1/readelf)
* [objdump(1)](https://linux.die.net/man/1/objdump)
* [objcopy(1)](https://linux.die.net/man/1/objcopy)
* [strip(1)](https://linux.die.net/man/1/strip)
* [nm(1)](https://linux.die.net/man/1/nm)
* [strings(1)](https://linux.die.net/man/1/strings)
* [gdb(1)](https://linux.die.net/man/1/gdb) (and [lldb](https://lldb.llvm.org), as well as the
  [rust-gdb](https://github.com/rust-lang/rust/blob/master/src/etc/rust-gdb) and
  [rust-lldb](https://github.com/rust-lang/rust/blob/master/src/etc/rust-lldb) wrappers)
* [ld.so(8)](https://linux.die.net/man/8/ld.so)
* [g++(1)](https://linux.die.net/man/1/g++)
* [ar(1)](https://linux.die.net/man/1/ar)
* [ld(1)](https://linux.die.net/man/1/ld) (and [mold](https://github.com/rui314/mold),
  [gold](https://www.gnu.org/software/binutils/), [lld](https://lld.llvm.org/))
* [ldd(1)](https://linux.die.net/man/1/ldd) (and [wrapper
  script](https://github.com/Notgnoshi/dotfiles/blob/master/bin/xldd) to adapt to a
  cross-compilation toolchain)
* [proc(5)](https://linux.die.net/man/5/proc)
* [elf(5)](https://linux.die.net/man/5/elf)
* [xxd(1)](https://linux.die.net/man/1/xxd)
* [strace(1)](https://linux.die.net/man/1/strace)
* [file(1)](https://linux.die.net/man/1/file)

## Specific questions about ELF files

The compilation process, binary artifacts thereof, process runtime (startup, dynamic symbol
resolution, debugging, stack unwinding, coredumps, etc) are all somewhat interconnected, and
difficult to give enough of a summary to be dangerous, because of the sheer amount of information
needed to cover it all.

Thus, despite my preference to build understanding from the bottom up in the general case, I think
it makes most sense to ask (and answer) specific questions in this particular case.

* [What's an ELF file?](./whats-an-elf-file.md)
* [How are processes loaded into memory?](./how-are-processes-loaded-into-memory.md)
* TODO - What is the compilation process?

  for traditional Linux GCC / LLVM builds and ARM Keil builds (I bet they're remarkably similar, but
  get nitty gritty with scatter files (linker scripts), axf, map, and ihex file pipeline)

  <!-- ![autotools](https://wiki.gnome.org/Apps/Dia/Examples?action=AttachFile&do=get&target=VictorStinnerAutotools.png) -->

* TODO - What _is_ linking?

  what do linkers _do_? How? Why?

* TODO - How is main() executed?

  Lots of complexity provided by libc to do global initialization/deinitialization, exception
  handling, etc. before `main()` is actually invoked. Should cover the GCC default linker script,
  and `.init_array`.

* TODO - How are dynamic symbols resolved?

  and how to debug dynamic symbol resolution?

* TODO - How do ELF files differ between different compilers?

  https://godbolt.org for comparing disassembly.

* TODO - How are debug symbols used?

  TIL about llvm-dwarfdump

* TODO - How are coredumps generated?

  Include `gcore` in GDB

* TODO - How do exceptions work?

  and Rust panics?

* TODO - libstdc++, libcxx, libcxxabi, compiler-rt, libunwind, libgcc, libgcc_s, libatomic, libsupc++

  https://clang.llvm.org/docs/Toolchain.html

* TODO - How does code coverage work?
* TODO - Why are Rust binaries so large?

  Compiling an empty binary that returns 0 is several megabytes, despite there being _nothing_ to
  statically link...

## Resources
* https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
* https://refspecs.linuxbase.org/elf/elf.pdf
* https://fasterthanli.me/series/making-our-own-executable-packer/part-1
* https://fasterthanli.me/articles/a-dynamic-linker-murder-mystery
* https://www.uclibc.org/docs/elf-64-gen.pdf
* https://github.com/corkami/pics/tree/master/binary/elf101
* https://jvns.ca/blog/2014/09/06/how-to-read-an-executable/
* http://www.bottomupcs.com/chapter07.xhtml
* https://danluu.com/edit-binary/
* https://lwn.net/Articles/276782/
* https://akkadia.org/drepper/dsohowto.pdf
* https://www.youtube.com/watch?v=dOfucXtyEsU
* https://lock.cmpxchg8b.com/linux123.html
* http://www.muppetlabs.com/~breadbox/software/tiny/teensy.html
