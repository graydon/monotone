
mtn_setup()

getfile("testfile")
addfile("testfile")
commit()
anc = base_revision()

getfile("dont_merge")
getfile("left")
getfile("right")
getfile("merged")

copyfile("dont_merge", "testfile")
commit()

revert_to(anc)
copyfile("left", "testfile")
commit()
left = base_revision()

revert_to(anc)
copyfile("right", "testfile")
commit()
right = base_revision()

check(mtn("explicit_merge", left, right, "otherbranch"), 0, false, false)

-- Check that it didn't end up on our current branch, i.e. update doesn't do anything
check(mtn("update"), 0, false, false)
check(samefile("right", "testfile"))

check(mtn("checkout", "--branch=otherbranch", "otherbranch_co"), 0, false, false)
check(samefile("merged", "otherbranch_co/testfile"))
