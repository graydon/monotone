
mtn_setup()

addfile("testfile", "blah blah")
commit()
R1=base_revision()

-- check that changed (added) file is listed in the log output
check(mtn("log"), 0, true, false)
check(qgrep("testfile", "stdout"))

-- and that it has been excluded by --no-files
check(mtn("log", "--no-files"), 0, true, false)
check(not qgrep("testfile", "stdout"))

-- add create some divergence...
writefile("testfile", "stuff stuff")
commit()

revert_to(R1)

addfile("nufile", "moo moo")
commit()

-- ...and now merge it cleanly
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
R2=base_revision()

-- check that merge is included by default
check(mtn("log"), 0, true, false)
check(qgrep("^[\\|\\\\\/ ]+Revision.*"..R2, "stdout"))

-- and that it is excluded by --no-merges
check(mtn("log", "--no-merges"), 0, true, false)
check(not qgrep("^[\\|\\\\\/ ]+Revision.*"..R2, "stdout"))
