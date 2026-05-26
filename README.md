# mem2reg-sccp

LLVM out-of-tree passes implementing **Memory to Register Promotion (mem2reg)** and **Sparse Conditional Constant Propagation (SCCP)** as a university compiler project.

## Passes

### mem2reg
Promotes `alloca`/`store`/`load` sequences into SSA register form by inserting `phi` nodes at dominance frontiers, eliminating unnecessary memory accesses.

### SCCP
Sparse Conditional Constant Propagation — a dataflow analysis that simultaneously propagates constant values and removes unreachable basic blocks, producing tighter results than simple constant folding.

## Project Structure

```
mem2reg-sccp/
├── include/        # Header files
├── src/
│   ├── mem2reg/    # mem2reg pass implementation
│   └── sccp/       # SCCP pass implementation
└── docs/           # Report and notes
```

## Requirements

- LLVM 17+ (with headers and `LLVMConfig.cmake`)
- CMake 3.20+
- C++17 compiler

## Authors
- [Lazar Dunjić 265/2021](https://github.com/lazardunjic)
- [Milica Mladenović 349/2021](https://github.com/milicaamladenovic)
- [Luka Nedeljković 147/2021](https://github.com/nedeljko02)
- [Filip Dramićanin 303/2021](https://github.com/doktorfilip1)