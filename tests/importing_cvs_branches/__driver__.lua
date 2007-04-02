
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

writefile("file1.0", "version 0 of test file1\n")
writefile("file1.1", "version 1 of test file1\n")
writefile("file1.2", "version 2 of test file1\n")
writefile("file2.0", "version 0 of test file2\n")
writefile("file2.1", "version 1 of test file2\n")

writefile("changelog.0", "first changelog entry\n")
writefile("changelog.1", readfile("changelog.0").."second changelog\n")
writefile("changelog.2", readfile("changelog.1").."third changelog -not on branch-\n")
writefile("changelog.3", readfile("changelog.1").."third changelog -on branch-\n")

F1SHA0=sha1("file1.0")
F1SHA1=sha1("file1.1")
F1SHA2=sha1("file1.2")
F2SHA0=sha1("file2.0")
F2SHA1=sha1("file2.1")
CSHA0=sha1("changelog.0")
CSHA1=sha1("changelog.1")
CSHA2=sha1("changelog.2")
CSHA3=sha1("changelog.3")


-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check if all branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.branched"}))

-- checkout the imported repository into maindir and branchdir
check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.branched", "branchdir"), 0, false, false)

-- check for correctness of the files in the main tree
check(samefile("file1.2", "maindir/file1"))
check(samefile("file2.0", "maindir/file2"))
check(samefile("changelog.2", "maindir/changelog"))

-- check for correctness of the files in the branch
check(samefile("file1.1", "branchdir/file1"))
check(samefile("file2.1", "branchdir/file2"))
check(samefile("changelog.3", "branchdir/changelog"))

-- get the log of the branch to check for correct branchpoint
check(indir("branchdir", mtn("log")), 0, true, false)
check(grep("commit on branch", "stdout"), 0, false, false)
xfail_if(true, grep("initial import", "stdout"), 0, false, false)
