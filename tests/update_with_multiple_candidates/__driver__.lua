
mtn_setup()

-- Make sure that when there are multiple plausible heads, update
-- fails, and prints some informative message listing the heads.

addfile("testfile", "foo")
commit()
anc = base_revision()

addfile("file1", "bar")
commit()
left = base_revision()

revert_to(anc)

addfile("file1", "baz")
commit()
right = base_revision()

revert_to(anc)

-- There are two possible candidates, so our update should fail.
remove("file1")
check(mtn("update"), 1, false, true)
check(not exists("file1"))
-- Make sure that the failure message listed the possibilities
check(qgrep(left, "stderr"))
check(qgrep(right, "stderr"))
check(not qgrep(anc, "stderr"))

-- Check also when one side is deeper than the other...
revert_to(left)
addfile("file2", "blah blah blah")
commit()
left2 = base_revision()

revert_to(anc)
remove("file1")
remove("file2")
check(mtn("update"), 1, false, true)
check(not exists("file1"))
check(not exists("file2"))
-- Make sure that the failure message listed the possibilities
check(not qgrep(left, "stderr"))
check(qgrep(left2, "stderr"))
check(qgrep(right, "stderr"))
check(not qgrep(anc, "stderr"))
