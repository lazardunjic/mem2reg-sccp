; KORAK 1: nepoznat uslov (argument -> overdefined) -> OBE grane su dostizne.
;
; Ocekivano (verbose):
;   entry EXECUTABLE, then EXECUTABLE, else EXECUTABLE
;   entry -> then : live
;   entry -> else : live

define i32 @unknown_cond(i1 %c) {
entry:
  br i1 %c, label %then, label %else
then:
  ret i32 1
else:
  ret i32 2
}
