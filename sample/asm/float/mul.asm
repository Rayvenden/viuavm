; This script tests float multiplication.

.def: main 0
    fstore 1 4.0
    fstore 2 2.001
    fmul 1 2 3
    print 3
    end
.end

frame 0
call main
halt