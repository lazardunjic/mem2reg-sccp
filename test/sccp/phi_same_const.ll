; KORAK 2: obe grane dostizne (nepoznat uslov), ali oba ulaza ista konstanta.
; meet(5,5) = const 5  ->  %p je konstanta i pored grananja.
;
; Ocekivano:
;   svi blokovi EXECUTABLE
;   %p ... => const 5

define i32 @phi_same_const(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi i32 [ 5, %a ], [ 5, %b ]
  ret i32 %p
}
