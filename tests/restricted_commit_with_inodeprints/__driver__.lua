
mtn_setup()

-- this test is a bug report. the problem is that inodeprints tries to update its
-- cache for all files in the complete manifest, but a restricted commit can
-- succeed with missing files if they are excluded. subsequently the inodeprint
-- update fails because it can't build a complete manifest due to the missing
-- files.

-- one solution is to let inodeprints update its cache only for files that are
-- included in the restriction, which seems to be safe. the only gaurentee that
-- inodeprints mode makes is that if a file's current inodeprint matches its
-- cached inodprint then it has not changed. i.e. for a missing file, the cache
-- would not be updated but the old cached value can't possibly equal the current
-- value since the file does not exist and cannot have an inodeprint.

-- also, it may be a useful feature (?) to allow refresh_inodeprints to update
-- its cache for a restricted set of files by allowing paths on the command line
-- to establish a restriction.

addfile("file1", "file1")

commit()

-- enable inodeprints mode
check(mtn("refresh_inodeprints"), 0, false, false)

addfile("file2", "file2")

-- create a missing file

remove("file1")

-- restricted commit of file2 succeeds with file1 missing
-- but the corresponding inodeprint update fails

xfail_if(true, mtn("commit", "--message=file2", "file2"),  0, false, false)
