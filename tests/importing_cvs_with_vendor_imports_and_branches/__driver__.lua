
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

writefile("file1.1", "version 1 of test file1\n")
writefile("file1.2", "version 2 of test file1\n")
writefile("file2.0", "version 0 of test file2\n")
writefile("file2.1", "version 1 of test file2\n")

writefile("changelog.0", "first changelog entry\n")
writefile("changelog.1", readfile("changelog.0").."second changelog\n")
writefile("changelog.2", readfile("changelog.1").."third changelog -not on branch-\n")
writefile("changelog.3", readfile("changelog.1").."third changelog -on branch-\n")


-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.branched", "branchdir"), 0, false, false)

check(samefile("file1.1", "maindir/file1"))
check(samefile("file2.0", "maindir/file2"))
check(samefile("changelog.2", "maindir/changelog"))

check(samefile("file1.2", "branchdir/file1"))
check(samefile("file2.1", "branchdir/file2"))
check(samefile("changelog.3", "branchdir/changelog"))
