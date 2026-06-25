#!/bin/bash
#A:Testovi za projekat:
#B:tests/*.c        -> pun pipeline (mem2reg + phi -> sccp), diff before/after
#test/sccp/*.ll   -> samo sccp (rucno pisani SSA testovi za SCCP), diff before/after
set -e

MEM2REG_SO="build/src/mem2reg/CustomMem2Reg.so"
SCCP_SO="build/src/sccp/CustomSCCP.so"
CLANG="clang-14"
OPT="opt-14"
OUT="build/test-out" 

mkdir -p "$OUT"

#build
echo "==> build"
cmake --build build

shopt -s nullglob

#deo A
echo ""
echo "==> A) pipeline mem2reg -> sccp  (tests/*.c)"
ctests=(tests/*.c)
if [ ${#ctests[@]} -eq 0 ]; then
    echo "  (nema tests/*.c)"
fi

for src in "${ctests[@]}"; do
    name="$(basename "${src%.c}")"
    ll="$OUT/${name}.ll"
    after="$OUT/${name}_pipeline.ll"

    echo ""
    echo "----- ${src} -----"
    "$CLANG" -S -emit-llvm -Xclang -disable-O0-optnone "$src" -o "$ll"
    "$OPT" -load "$MEM2REG_SO" -load "$SCCP_SO" -enable-new-pm=0 \
           -custom-mem2reg -custom-phi -custom-sccp \
           -S "$ll" -o "$after"

    echo "  --- diff (before -> after) ---"
    if diff <(tail -n +2 "$ll") <(tail -n +2 "$after") > /dev/null; then
        echo "  (bez promena)"
    else
        diff <(tail -n +2 "$ll") <(tail -n +2 "$after") | sed 's/^/  /' || true
    fi
done

#deo B
echo ""
echo "==> B) sccp  (test/sccp/*.ll)"
slltests=(test/sccp/*.ll)
if [ ${#slltests[@]} -eq 0 ]; then
    echo "  (nema test/sccp/*.ll)"
fi

for src in "${slltests[@]}"; do
    name="$(basename "${src%.ll}")"
    after="$OUT/${name}_sccp.ll"

    echo ""
    echo "----- ${src} -----"
    "$OPT" -load "$SCCP_SO" -enable-new-pm=0 \
           -custom-sccp -S "$src" -o "$after"

    echo "  --- diff (before -> after) ---"
    if diff <(tail -n +2 "$src") <(tail -n +2 "$after") > /dev/null; then
        echo "  (bez promena)"
    else
        diff <(tail -n +2 "$src") <(tail -n +2 "$after") | sed 's/^/  /' || true
    fi
done

echo ""
echo "==>FINISHED!"