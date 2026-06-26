#!/bin/bash
set -u

MEM2REG_SO="build/src/mem2reg/CustomMem2Reg.so"
SCCP_SO="build/src/sccp/CustomSCCP.so"
CLANG="clang-14"
OPT="opt-14"
OUT="build/test-out"

UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

mkdir -p "$OUT"
shopt -s nullglob

PASS=0
FAIL=0
MISS=0

normalize() { grep -vE '^(; ModuleID|source_filename)' "$1"; }

check() {
    local actual="$1" expected="$2" name="$3"
    if [ "$UPDATE" -eq 1 ]; then
        normalize "$actual" > "$expected"
        echo "  UPDATED  $name"
        return
    fi
    if [ ! -f "$expected" ]; then
        echo "  NO-EXP   $name   (pokreni ./run.sh --update)"
        MISS=$((MISS+1))
        return
    fi
    if diff <(normalize "$actual") "$expected" > /dev/null; then
        echo "  PASS     $name"
        PASS=$((PASS+1))
    else
        echo "  FAIL     $name"
        diff <(normalize "$actual") "$expected" | sed 's/^/      /' | head -40
        FAIL=$((FAIL+1))
    fi
}

echo "==> build"
cmake --build build || { echo "BUILD FAILED"; exit 1; }

echo ""
echo "==> A) pipeline mem2reg -> sccp  (tests/*.c)"
for src in tests/*.c; do
    name="$(basename "${src%.c}")"
    ll="$OUT/${name}.ll"
    after="$OUT/${name}_pipeline.ll"
    "$CLANG" -S -emit-llvm -Xclang -disable-O0-optnone "$src" -o "$ll"
    "$OPT" -load "$MEM2REG_SO" -load "$SCCP_SO" -enable-new-pm=0 \
           -custom-mem2reg -custom-phi -custom-sccp -S "$ll" -o "$after" 2>/dev/null
    check "$after" "tests/${name}.expected" "$name"
done

echo ""
echo "==> B) sccp  (test/sccp/*.ll)"
for src in test/sccp/*.ll; do
    name="$(basename "${src%.ll}")"
    after="$OUT/${name}_sccp.ll"
    "$OPT" -load "$SCCP_SO" -enable-new-pm=0 \
           -custom-sccp -S "$src" -o "$after" 2>/dev/null
    check "$after" "test/sccp/${name}.expected" "$name"
done

echo ""
if [ "$UPDATE" -eq 1 ]; then
    echo "==> expected fajlovi regenerisani"
else
    echo "==> rezime: PASS=$PASS  FAIL=$FAIL  NO-EXP=$MISS"
    [ "$FAIL" -gt 0 ] && exit 1
fi