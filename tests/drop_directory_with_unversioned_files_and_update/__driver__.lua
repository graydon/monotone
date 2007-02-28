mtn_setup()

mkdir("subdir")
check(mtn("add", "subdir"), 0, false, false)
addfile("subdir/file", "test data")
check(mtn("commit", "-m", "create the base"), 0, false, false)

base0 = base_revision()

check(mtn("drop", "-R", "subdir"), 0, false, false)
check(mtn("commit", "-m", "subdir removed"), 0, false, false)

-- Now, move back to the first revision, add an unversioned
-- file in subdir and try to update to head

check(mtn("update", "-r", base0), 0, false, false)
writefile("subdir/unversioned", "")

check(mtn("update"), 1, false, false)
