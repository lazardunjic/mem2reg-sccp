; KORAK 2: obe grane dostizne (nepoznat uslov), razlicite konstante.
; meet(1,2) = overdefined  ->  %p NIJE konstanta.
;
; Ocekivano:
;   svi blokovi EXECUTABLE
;   %p ... => overdefined

define i32 @phi_overdef(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi i32 [ 1, %a ], [ 2, %b ]
  ret i32 %p
}
