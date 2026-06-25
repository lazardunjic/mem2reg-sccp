; KORAK 3: folding (C) + rewrite (D) zajedno.
;   %x = 2+3 = 5 ; %c = (5 == 5) = true ; br true -> 'dead' grana otpada
;   %r = phi [ %x, %live ], [ 99, %dead ]  -> dead ulaz otpada -> %r = 5
;
; Ocekivano posle passa: ostaje samo entry+live+merge, sve konstantno,
;   ret i32 5  (bez ijedne aritmeticke instrukcije, bez grananja)

define i32 @combined() {
entry:
  %x = add i32 2, 3
  %c = icmp eq i32 %x, 5
  br i1 %c, label %live, label %dead
live:
  br label %merge
dead:
  br label %merge
merge:
  %r = phi i32 [ %x, %live ], [ 99, %dead ]
  ret i32 %r
}
