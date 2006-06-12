
mtn_setup()

-- Turn them on
writefile("_MTN/inodeprints")

check(mtn("diff"), 0, true, false)
check(qgrep("no changes", "stdout"))

addfile("testfile", "blah blah")

check(mtn("diff"), 0, true, false)
check(qgrep("blah blah", "stdout"))

commit()

-- Something should have been written to it
check(fsize("_MTN/inodeprints") ~= 0)
copyfile("_MTN/inodeprints", "ip1")

-- And stuff should still work
check(mtn("diff"), 0, true, false)
check(qgrep("no changes", "stdout"))

writefile("testfile", "stuff stuff")

check(mtn("diff"), 0, true, false)
check(qgrep("stuff stuff", "stdout"))

-- Make sure partial commit doesn't screw things up
addfile("otherfile", "other stuff")
check(mtn("commit", "otherfile", "--message=foo"), 0, false, false)

-- Should have changed the inodeprints file
check(not samefile("_MTN/inodeprints", "ip1"))

-- Still should think testfile is modified
check(mtn("diff"), 0, true, false)
check(qgrep("stuff stuff", "stdout"))
