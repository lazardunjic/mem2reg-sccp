; KORAK 2 (glavni SCCP poen): phi ciji je jedan predecessor MRTAV.
; Uslov je konstantan 'true' -> grana %b je unreachable, pa se ulaz [20,%b]
; NE racuna. Obican constant-folding bi video 10 i 20 -> odustao (overdefined);
; SCCP zna da je %b mrtav -> %p = const 10.
;
; Ocekivano:
;   blokovi: entry, a, m EXECUTABLE ; b DEAD
;   %p ... => const 10

define i32 @phi_dead_pred() {
entry:
  br i1 true, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi i32 [ 10, %a ], [ 20, %b ]
  ret i32 %p
}
