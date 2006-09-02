
mtn_setup()

-- This is trivial on case-sensitive filesystems, but a little trickier
-- on case-preserving ones.

mkdir("FooBar")
addfile("FooBar/testfile", "blah blah")
commit()

writefile("FooBar/testfile", "stuff stuff")

check(indir("FooBar", mtn("commit", ".", "-mfoo")), 0, false, false)

check(mtn("diff"), 0, true, false)
check(qgrep("no changes", "stdout"))
