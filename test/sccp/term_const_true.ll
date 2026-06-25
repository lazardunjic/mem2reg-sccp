; KORAK 1: konstantan uslov 'true' -> 'else' grana je unreachable.
;
; Pokretanje (u Linux/WSL okruzenju gde je LLVM 14):
;   opt-14 -load build/src/sccp/CustomSCCP.so -enable-new-pm=0 \
;          -custom-sccp -custom-sccp-verbose test/sccp/term_const_true.ll -S -o /dev/null
;
; Ocekivano (verbose):
;   entry EXECUTABLE, then EXECUTABLE, else DEAD
;   entry -> then : live
;   entry -> else : dead

define i32 @const_true() {
entry:
  br i1 true, label %then, label %else
then:
  ret i32 1
else:
  ret i32 2
}
