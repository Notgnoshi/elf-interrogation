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
