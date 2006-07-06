
mtn_setup()

addfile("testfile", "blah blah")
commit()

check(mtn("update", "--revision=73070030f7b0d0f3d4ee02545d45ca4bbe5e189f"), 1, false, false)
