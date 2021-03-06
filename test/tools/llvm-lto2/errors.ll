; RUN: llvm-as %s -o %t.bc
; RUN: not llvm-lto2 -o %t2.o %t.bc 2>&1 | FileCheck --check-prefix=ERR1 %s
; RUN: not llvm-lto2 -o %t2.o -r %t.bc,foo,p -r %t.bc,bar,p %t.bc 2>&1 | FileCheck --check-prefix=ERR2 %s
; RUN: not llvm-lto2 -o %t2.o -r %t.bc,foo,q %t.bc 2>&1 | FileCheck --check-prefix=ERR3 %s
; RUN: not llvm-lto2 -o %t2.o -r foo %t.bc 2>&1 | FileCheck --check-prefix=ERR4 %s

; ERR1: missing symbol resolution for {{.*}}.bc,foo
; ERR2: unused symbol resolution for {{.*}}.bc,bar
; ERR3: invalid character q in resolution: {{.*}}.bc,foo
; ERR4: invalid resolution: foo
@foo = global i32 0
