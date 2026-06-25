; KORAK 1: konstantan uslov 'false' -> 'then' grana je unreachable.
;
; Ocekivano (verbose):
;   entry EXECUTABLE, then DEAD, else EXECUTABLE
;   entry -> then : dead
;   entry -> else : live

define i32 @const_false() {
entry:
  br i1 false, label %then, label %else
then:
  ret i32 1
else:
  ret i32 2
}
