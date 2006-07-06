
mtn_setup()

addfile("testfile", "blah blah")
commit()
rev = base_revision()

writefile("testfile", "other other")
commit("otherbranch")

-- This update should fail (because not only does it have no update
-- candidate, but it's currently at an off-branch revision); and should
-- not update the _MTN/options
check(mtn("update", "-b", "testbranch"), 1, false, false)
check(qgrep("otherbranch", "_MTN/options"))
check(not qgrep("testbranch", "_MTN/options"))

revert_to(rev, "testbranch")
check(not qgrep("otherbranch", "_MTN/options"))
check(qgrep("testbranch", "_MTN/options"))

check(mtn("update", "-b", "otherbranch"), 0, false, false)
check(qgrep("otherbranch", "_MTN/options"))
check(not qgrep("testbranch", "_MTN/options"))
