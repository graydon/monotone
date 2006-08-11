
mtn_setup()

addfile("testfile", "version 0 data")
commit()

addfile("testfile2", "other data")
commit("otherbranch")

check(mtn("db", "set_epoch", "testbranch", "12345"), 1, false, false)
check(mtn("db", "set_epoch", "testbranch", string.rep("a", 40)), 0, false, false)
check(mtn("db", "set_epoch", "otherbranch", string.rep("b", 40)), 0, false, false)

check(mtn("list", "epochs"), 0, true)
check(qgrep("testbranch", "stdout"))
check(qgrep("otherbranch", "stdout"))
check(mtn("list", "epochs", "testbranch"), 0, true)
check(qgrep("testbranch", "stdout"))
check(not qgrep("otherbranch", "stdout"))
