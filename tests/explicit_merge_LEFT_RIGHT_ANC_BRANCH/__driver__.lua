
mtn_setup()

-- This test relies on a command-line mode which has, for the time
-- being, been disabled. Possibly no need to revive it.

addfile("testfile", "a\nOLD\nc\nd\ne\n")
commit()
bad_anc = base_revision()

writefile("testfile", "a\nb\nc\nd\ne\n")
commit()
good_anc = base_revision()

writefile("testfile", "a\nNEW\nc\nd\ne\n")
commit()
left = base_revision()

revert_to(good_anc)

writefile("testfile", "a\nb\nc\nNEW\ne\n")
commit()
right = base_revision()

-- This should fail:
xfail_if(true, mtn("explicit_merge", left, right, bad_anc, "testbranch"), 1, false, false)
-- But this should work:
xfail_if(true, mtn("explicit_merge", left, right, good_anc, "testbranch"), 0, false, false)
-- And produce the logical result:
check(mtn("update"), 0, false, false)
writefile("expected", "a\nNEW\nc\nNEW\ne\n")
check(samefile("expected", "testfile"))
