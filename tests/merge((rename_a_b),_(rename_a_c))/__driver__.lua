
mtn_setup()

-- Should cause a merge conflict.

addfile("a", "blah blah")
commit()
base = base_revision()

rename("a", "b")
check(mtn("rename", "a", "b"), 0, false, false)
commit()

rename("b", "a")
revert_to(base)

rename("a", "c")
check(mtn("rename", "a", "c"), 0, false, false)
commit()

check(mtn("merge", "--branch=testbranch"), 1, false, false)
