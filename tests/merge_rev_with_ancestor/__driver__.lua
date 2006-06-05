
mtn_setup()

-- The idea here is that if we have, say, A -> B -> C, then merging A
-- and C should not be possible, because it creates a weird graph with
-- no clear meaning.

addfile("testfile", "0")
commit()
r0 = base_revision()

writefile("testfile", "1")
commit()
r1 = base_revision()

writefile("testfile", "2")
commit()
r2 = base_revision()

check(mtn("explicit_merge", r0, r1, "testbranch"), 1, false, false)
check(mtn("explicit_merge", r1, r0, "testbranch"), 1, false, false)
