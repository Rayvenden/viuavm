; This script tests support for float equality checking.
; Its expected output is "true".

.def: main 0
    fstore 1 0.69
    fstore 2 0.69
    feq 1 2

    ; should be true
    print 1
    end
.end

frame 0
call main
halt