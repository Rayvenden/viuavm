# Viua VM

[![Build Status](https://travis-ci.org/marekjm/viuavm.svg)](https://travis-ci.org/marekjm/viuavm)

A simple, bytecode driven, register-based virtual machine.

I develop Viua VM to learn how such software is created and
to help myself in my computer language implementation studies.

----

## Programming in Viua

Viua can be programmed in an assembler-like language which must then be compiled into bytecode.
Typical code-n-debug cycle is shown below (assuming current working directory
is the local clone of Viua repository):

```
vi some_file.asm
./build/bin/vm/asm -o some_file.out some_file.asm
./build/bin/vm/cpu some_file.out
./build/bin/vm/vdb some_file.out
```


----

# Development

Some development-related information.
Required tools:

* `g++`: GNU Compiler Collection's C++ compiler version 4.9 and above (mandatory),
* `clang++`: clang C++ compiler version 3.6.1 and above (if not using GCC, clang builds are **not** guaranteed to work),
* `python`: Python programming language 3.x for test suite (optional),
* `valgrind`: for memory leak testing (optional; by default enabled, disabling required setting `MEMORY_LEAK_CHECKS_ENABLE` variable in `tests/tests.py` to `False`),


## Compilation

Before compiling, Git submodule for `linenoise` library must be initialised.

Compilation is simple and can be executed by typing `make` in the shell.
Full, clean compilation can also be performed by the `./scripts/recompile` script.
The script will run `make clean` production, detect number of cores the machine compilation is done on has, and
run `make` with `-j` option adjusted to take advantage of multithreaded `make`-ing.

Incremental recompilation can be performed with either `make` or `./scripts/compile` (the latter will detect the number of
cores available and adjust `-j` option in Make).


## Testing

There is a simple test suite located in `tests/` directory.
It can be invoked by `make test` command.
Python 3 must be installed on the machine to run the tests.

Code used for unit tests can be found in `sample/` directory.


### Viua development scripts

In the `scripts/` directory, you can find scripts that are used during development of Viua.
The shell installed in dev environment is ZSH but the scripts should be compatible with BASH as well.


## Git workflow

Each feature and fix is developed in a separate branch.
Bugs which are discovered during development of a certain feature,
may be fixed in the same branch as their parent issue.
This is also true for small features.

**Branch structure:**

- `master`: master branch - contains stable, working version of VM code,
- `devel`: development branch - all fixes and features are first merged here,
- `issue/<number>/<slug>`: for issues,


Explained with arrows:

```
issue/* ←----→ devel
                 |
                 ↓
              master
```


----

## License

The code is licensed under GNU GPL v3.
