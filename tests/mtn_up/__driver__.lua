
mtn_setup()

-- People expect 'mtn up' to run update.
-- Make sure it does.

addfile("testfile", "blah blah")
commit()
rev0 = base_revision()

writefile("testfile", "other stuff")
commit()
rev1 = base_revision()

check(mtn("checkout", "--branch=testbranch", "--revision", rev0, "codir"), 0, false, false)
check(indir("codir", mtn("up")), 0, false, false)
check(samefile("testfile", "codir/testfile"))
