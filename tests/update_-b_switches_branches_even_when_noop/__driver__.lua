
mtn_setup()

addfile("testfile", "blah blah")
commit()

RID1=base_revision()

check(mtn("cert", RID1, "branch", "otherbranch"), 0, false, false)

check(qgrep("testbranch", "_MTN/options"))
check(not qgrep("otherbranch", "_MTN/options"))

check(mtn("update", "-b", "otherbranch"), 0, false, false)
RID2=base_revision()
check(RID1 == RID2)

check(not qgrep("testbranch", "_MTN/options"))
check(qgrep("otherbranch", "_MTN/options"))
