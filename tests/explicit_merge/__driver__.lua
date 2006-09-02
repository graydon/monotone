
mtn_setup()

check(get("testfile"))
addfile("testfile")
commit()
anc = base_revision()

check(get("dont_merge"))
check(get("left"))
check(get("right"))
check(get("merged"))

copy("dont_merge", "testfile")
commit()

revert_to(anc)
copy("left", "testfile")
commit()
left = base_revision()

revert_to(anc)
copy("right", "testfile")
commit()
right = base_revision()

check(mtn("explicit_merge", left, right, "otherbranch"), 0, false, false)

-- Check that it didn't end up on our current branch, i.e. update doesn't do anything
check(mtn("update"), 0, false, false)
check(samefile("right", "testfile"))

check(mtn("checkout", "--branch=otherbranch", "otherbranch_co"), 0, false, false)
check(samefile("merged", "otherbranch_co/testfile"))
