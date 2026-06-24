; KORAK 1: switch na konstanti 7 -> ziva je samo grana 'c7', ostalo unreachable.
;
; Ocekivano (verbose):
;   entry EXECUTABLE, c7 EXECUTABLE, c1 DEAD, def DEAD
;   entry -> c7 : live ; sve ostale ivice : dead

define i32 @switch_const() {
entry:
  switch i32 7, label %def [
    i32 1, label %c1
    i32 7, label %c7
  ]
c1:
  ret i32 10
c7:
  ret i32 70
def:
  ret i32 0
}
