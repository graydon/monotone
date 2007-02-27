mtn_setup()

addfile("file1", "original file1")
commit()

-- setup 
check(mtn("rename", "--bookkeep-only", "file1", "file2"), 0, true, true)
addfile("file1", "new file1")

-- first check; revert the new file which will end up reverting both changes
-- XXX is this really desired behaviour?
check(mtn("revert", "file1"), 0, true, true)
-- check results

-- setup 
check(mtn("rename", "--bookkeep-only", "file1", "file3"), 0, true, true)
addfile("file1", "new file1")

-- second check; revert the renamed file which will.. uh.. trip an I()
xfail(check(mtn("revert", "file3"), 3, true, true))
