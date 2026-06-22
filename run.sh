#!/bin/bash
# Build + pokreni CustomMem2Reg nad svim tests/*.c, prikaži before/after diff.
set -e

PASS_SO="build/src/mem2reg/CustomMem2Reg.so"
PASS_NAME="custom-mem2reg"
CLANG="clang-14"
OPT="opt-14"

# 1. build
echo "==> build"
cmake --build build

# 2. prođi kroz sve .c u tests/
echo ""
echo "==> testovi"
shopt -s nullglob
tests=(tests/*.c)
if [ ${#tests[@]} -eq 0 ]; then
    echo "Nema .c fajlova u tests/"
    exit 0
fi

for src in "${tests[@]}"; do
    base="${src%.c}"                # tests/test1
    ll="${base}.ll"
    after="${base}_after.ll"

    echo ""
    echo "----- ${src} -----"

    # generiši IR (disable-O0-optnone da pass sme da radi nad -O0 izlazom)
    "$CLANG" -S -emit-llvm -Xclang -disable-O0-optnone "$src" -o "$ll"

    # pokreni pass (-custom-phi uključuje diamond φ; bezopasno za testove bez diamond-a)
    "$OPT" -load "$PASS_SO" -enable-new-pm=0 \
           -"$PASS_NAME" -custom-phi -custom-verbose -S "$ll" -o "$after"

    # diff (ignoriši prvu liniju ModuleID koja se uvek razlikuje)
    echo "  --- diff (before -> after) ---"
    if diff <(tail -n +2 "$ll") <(tail -n +2 "$after") > /dev/null; then
        echo "  (bez promena)"
    else
        diff <(tail -n +2 "$ll") <(tail -n +2 "$after") | sed 's/^/  /' || true
    fi
done

echo ""
echo "==> gotovo"
