
mtn_setup()

-- add a first file
addfile("file1", "contents of file1")
commit()

-- store the newly created revision id
REV1=base_revision()

-- check(mtn("--branch", "testbranch", "co", "codir"), 0, false, false)
-- writefile("codir/file2", "contents of file2")

-- add another file and commit
addfile("file2", "contents of file2")
commit()

-- change that new file
writefile("file2", "new contents of file2")

-- .. and upadte to the previous revision, which didn't have file2.
-- At the moment, this simply drops file2 and all changes to it.
--
-- See bug #15058

xfail(mtn("update", "-r", REV1), 1, true, true)

-- IMO, the correct curse of action should be to fail updating due to
-- a conflict.
check(exists("file2"))
check(samelines("file2", {"new contents of file2"}))

