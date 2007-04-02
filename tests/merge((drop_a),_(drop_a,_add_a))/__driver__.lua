
mtn_setup()

writefile("original", "some stuff here")
writefile("replaced", "the re-added file")
writefile("nonce", "...nothing here...")

copy("original", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

-- drop it
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

revert_to(base)

-- on the other side of the fork, drop it ...
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
-- we add this file so that we don't end up with the same revision as
-- our first "drop" commit
check(mtn("add", "nonce"), 0, false, false)
commit()

-- ... and add the replacement
-- on the other side of the fork, drop it
copy("replaced", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

check(mtn("merge"), 0, false, false)
check(mtn("checkout", "-b", "testbranch", "clean"), 0, false, false)

-- check that the file is the replacement one
check(samefile("clean/testfile", "replaced"))
-- just for good measure
check(samefile("clean/nonce", "nonce"))
