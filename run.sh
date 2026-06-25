#!/bin/bash
# End-to-end pipeline: mem2reg -> SCCP nad svim tests/*.c
set -e

MEM2REG_SO="build/src/mem2reg/CustomMem2Reg.so"
SCCP_SO="build/src/sccp/CustomSCCP.so"
CLANG="clang-14"
OPT="opt-14"

# 1. build
echo "==> build"
cmake --build build

# 2. prođi kroz sve .c u tests/
echo ""
echo "==> pipeline (mem2reg -> sccp)"
shopt -s nullglob
tests=(tests/*.c)
if [ ${#tests[@]} -eq 0 ]; then
    echo "Nema .c fajlova u tests/"
    exit 0
fi

for src in "${tests[@]}"; do
    base="${src%.c}"
    ll="${base}.ll"
    after="${base}_pipeline.ll"

    echo ""
    echo "----- ${src} -----"

    # generiši IR
    "$CLANG" -S -emit-llvm -Xclang -disable-O0-optnone "$src" -o "$ll"

    # oba passa redom: mem2reg (+phi) pa sccp; SCCP dump ide na stderr
    "$OPT" -load "$MEM2REG_SO" -load "$SCCP_SO" -enable-new-pm=0 \
           -custom-mem2reg -custom-phi -custom-sccp \
           -S "$ll" -o "$after"

    # diff originala i finalnog IR-a (ignoriši ModuleID liniju)
    echo "  --- diff (before -> after pipeline) ---"
    if diff <(tail -n +2 "$ll") <(tail -n +2 "$after") > /dev/null; then
        echo "  (bez promena u IR-u)"
    else
        diff <(tail -n +2 "$ll") <(tail -n +2 "$after") | sed 's/^/  /' || true
    fi
done

echo ""
echo "==> gotovo"