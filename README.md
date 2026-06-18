# mem2reg-sccp

LLVM out-of-tree passes implementing a subset of **Memory to Register Promotion (mem2reg)** and **Sparse Conditional Constant Propagation (SCCP)** as a university compiler project.

## Passes

### mem2reg
Promotes `alloca`/`store`/`load` sequences into SSA register form, eliminating unnecessary memory accesses. Implements a restricted subset:

1. **Single-store** — if a slot has exactly one store that dominates all its loads, replace the loads with the stored value and delete the `alloca` + store.
2. **Restricted φ** (`-matf-phi`) — for a clean `if/else` diamond with one store per branch, insert a single φ at the merge block and route loads via dominance. (Restricted to a 2-branch diamond; does **not** compute dominance frontiers.)
3. **Zero-store** — a slot read but never written: loads become `undef`, slot is deleted.
4. **Dead-store-only** — a slot written but never read: stores and slot are deleted.

### SCCP
Sparse Conditional Constant Propagation — a dataflow analysis that simultaneously propagates constant values and removes unreachable basic blocks, producing tighter results than simple constant folding.

## Project Structure

```
mem2reg-sccp/
├── include/        # Shared headers (if any)
├── src/
│   ├── mem2reg/    # MatfSimpleMem2Reg.cpp
│   └── sccp/       # MatfSCCP.cpp
├── tests/          # .c sources + .ll before/after
└── docs/           # Report and notes
```

## Requirements

- **LLVM 14** (with dev headers and `LLVMConfig.cmake`)
- CMake 3.20+
- C++17 compiler
- Legacy pass manager (passes run with `-enable-new-pm=0`)

> Run `./setup.sh` to install the toolchain on Ubuntu/Debian or Arch.

## Build

```
cmake -S . -B build -DLLVM_DIR=$(llvm-config --cmakedir) -G Ninja
cmake --build build
```

## Run

```
# generate IR (disable optnone so passes can run on -O0 output)
clang -S -emit-llvm -Xclang -disable-O0-optnone tests/test1.c -o tests/test1.ll

# mem2reg
opt -load build/src/mem2reg/MatfSimpleMem2Reg.so -enable-new-pm=0 \
    -matf-simple-mem2reg -S tests/test1.ll -o tests/test1_after.ll

# mem2reg with restricted phi
opt -load build/src/mem2reg/MatfSimpleMem2Reg.so -enable-new-pm=0 \
    -matf-simple-mem2reg -matf-phi -S tests/test1.ll -o tests/test1_after.ll

# SCCP
opt -load build/src/sccp/MatfSCCP.so -enable-new-pm=0 \
    -matf-sccp -S tests/test1.ll -o tests/test1_sccp.ll
```

## Authors
- [Lazar Dunjić 265/2021](https://github.com/lazardunjic)
- [Milica Mladenović 349/2021](https://github.com/milicaamladenovic)
- [Luka Nedeljković 147/2021](https://github.com/nedeljko02)
- [Filip Dramićanin 303/2021](https://github.com/doktorfilip1)