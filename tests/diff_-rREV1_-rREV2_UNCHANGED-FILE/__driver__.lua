
mtn_setup()

addfile("testfile", "blah blah")
addfile("otherfile", "foo bar")
commit()
R1=base_revision()

writefile("otherfile", "stuff stuff")
commit()
R2=base_revision()

check(mtn("diff", "-r", R1, "-r", R2, "testfile"), 0, true, false)
check(qgrep('no changes', "stdout"))
check(not qgrep("testfile", "stdout"))
check(not qgrep("otherfile", "stdout"))

check(mtn("diff", "-r", R1, "-r", R2), 0, true, false)
check(not qgrep("testfile", "stdout"))
check(qgrep("otherfile", "stdout"))

check(mtn("diff", "-r", R1, "-r", R2, "badfile"), 1, true, false)
