# mem2reg-sccp

Out-of-tree LLVM passes implementing **Memory to Register Promotion (mem2reg)** and **Sparse Conditional Constant Propagation (SCCP)**, as a compiler-construction course project.

## Passes

### mem2reg (`custom-mem2reg`)
Promotes `alloca`/`store`/`load` sequences into SSA register form, eliminating unnecessary memory accesses. Implements a restricted subset:

1. **Single-store** — if a slot has exactly one store that dominates all its loads, replace the loads with the stored value and delete the `alloca` + store.
2. **Restricted φ** (`-custom-phi`) — for a clean `if/else` diamond with one store per branch, insert a single φ at the merge block and route loads via dominance. Restricted to a 2-branch diamond; does **not** compute dominance frontiers.
3. **Zero-store** — a slot read but never written: loads become `undef`, slot is deleted.
4. **Dead-store-only** — a slot written but never read: stores and slot are deleted.

Promotability is decided by an escape analysis (`collectUses`): a slot is promotable only if every use is a direct, non-volatile load/store; any other use (GEP, call, pointer escape) disqualifies it.

### SCCP (`custom-sccp`)
Sparse Conditional Constant Propagation — a dataflow analysis that simultaneously propagates constant values and removes unreachable basic blocks, producing tighter results than plain constant folding. A value is constant only along reachable edges, so a φ ignores inputs from dead predecessors.

Built on a three-level lattice (`Undef` → `Constant` → `Overdefined`) with a `meet` operator, two worklists (CFG edges and SSA edges), and a final rewrite step that replaces constant-valued instructions and prunes dead blocks.

## Requirements

- **LLVM 14** (dev headers + `LLVMConfig.cmake`)
- CMake 3.20+, a C++17 compiler, Ninja
- Legacy pass manager (passes run with `-enable-new-pm=0`)

Run `./setup.sh` to install the toolchain on Ubuntu/Debian or Arch.

## Build

```
cmake -S . -B build -DLLVM_DIR=$(llvm-config-14 --cmakedir) -G Ninja
cmake --build build
```

Produces `build/src/mem2reg/CustomMem2Reg.so` and `build/src/sccp/CustomSCCP.so`.

## Run

```
# generate IR from C (disable optnone so passes can run on -O0 output)
clang-14 -S -emit-llvm -Xclang -disable-O0-optnone tests/test1.c -o test1.ll

# mem2reg (with restricted phi)
opt-14 -load build/src/mem2reg/CustomMem2Reg.so -enable-new-pm=0 \
       -custom-mem2reg -custom-phi -S test1.ll -o test1_after.ll

# SCCP
opt-14 -load build/src/sccp/CustomSCCP.so -enable-new-pm=0 \
       -custom-sccp -S test1.ll -o test1_sccp.ll

# full pipeline: mem2reg -> SCCP
opt-14 -load build/src/mem2reg/CustomMem2Reg.so -load build/src/sccp/CustomSCCP.so \
       -enable-new-pm=0 -custom-mem2reg -custom-phi -custom-sccp \
       -S test1.ll -o test1_pipeline.ll
```

### Flags

| Flag | Pass | Effect |
|------|------|--------|
| `-custom-mem2reg` | mem2reg | run the mem2reg pass |
| `-custom-phi` | mem2reg | enable restricted diamond φ insertion |
| `-custom-verbose` | mem2reg | print per-alloca analysis logs |
| `-custom-sccp` | SCCP | run the SCCP pass |
| `-custom-sccp-verbose` | SCCP | print lattice state / executable blocks |

## Tests

- `tests/*.c` — C sources exercised through the full pipeline (mem2reg → SCCP).
- `test/sccp/*.ll` — hand-written SSA inputs targeting SCCP edge cases (constant branches, switch on constant, φ over dead/equal/conflicting predecessors, combined rewrite).

Run the whole suite:

```
./run.sh
```

It builds, runs the `.c` tests through the pipeline, and runs the `test/sccp/*.ll` tests through SCCP, printing a before/after diff for each.

## Project structure

```
mem2reg-sccp/
├── src/
│   ├── mem2reg/CustomMem2Reg.cpp
│   └── sccp/CustomSCCP.cpp
├── tests/          # .c pipeline tests
├── test/sccp/      # hand-written SCCP .ll tests
├── run.sh
└── setup.sh
```

## Authors
- [Lazar Dunjić 265/2021](https://github.com/lazardunjic)
- [Milica Mladenović 349/2021](https://github.com/milicaamladenovic)
- [Luka Nedeljković 147/2021](https://github.com/nedeljko02)
- [Filip Dramićanin 303/2021](https://github.com/doktorfilip1)
