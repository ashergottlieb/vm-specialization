# VM Specialization

This is accompanying material for my presentation _Deobfuscating VM-Obfuscated Code using Specialization_. The work here
is inspired by _Towards Static Analysis of Virtualization-Obfuscated Binaries_ by Johannes Kinder.

## Quickstart

`vm.cpp` contains the C++ virtual machine. The samples `vm.0.out`, `vm.1.out`, and `vm.2.out` in `artifacts/` are
variations of the compiled program at different levels of specialization. The `main` function in `vm.2.out` is the
completely deobfuscated program.

## Overview

This repository contains a handwritten C++ virtual machine and obfuscated VM-bytecode for a function which computes the
nth fibonacci number, located in `vm.cpp`
The VM can be compiled in 3 configurations, producing 3 different executables:

| Executable | Compile Flag | Description                                                                                                                                                                                                            |
|------------|--------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| vm.0.out   | -DSPEC=0     | The original program, resembling VM-obfuscated code.                                                                                                                                                                   |
| vm.1.out   | -DSPEC=1     | The program with the VM dispatch specialized to the VM bytecode and program counter. The resulting CFG resembles a control flow flattened CFG.                                                                         |
| vm.2.out   | -DSPEC=2     | Same as `vm.1.out`, but with additional specialization to yield the deobfuscated program.This is effectively a staged-interpreter or the first Futamura projection of the VM bytecode with respect to the interpreter. |

## Building

The Dockerfile can be used to obtain a build environment with clang.

To build the docker container and spawn a shell:

```
cd "$GIT_REPO_ROOT"
docker build . -t vmspec
# using `-v` will mount this git repository into the container.
docker run -v .:/build -ti vmspec sh
```

Then, run `make` within the shell to build the project.
