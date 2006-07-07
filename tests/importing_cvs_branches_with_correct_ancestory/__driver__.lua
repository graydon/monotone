
include("common/cvs.lua")
mtn_setup()

writefile("file1.0", "version 0 of test file1")
writefile("file1.1", "version 1 of test file1")
writefile("file1.2", "version 2 of test file1")
writefile("file2.0", "version 0 of test file2")
writefile("file2.1", "version 1 of test file2")

writefile("changelog.0", "first changelog entry\n")

writefile("changelog.1", "second changelog\n\n"..readfile("changelog.0"))
writefile("changelog.2", "third changelog -not on branch-\n\n"..readfile("changelog.1"))
writefile("changelog.3", "third changelog -on branch-\n\n"..readfile("changelog.1"))

F1SHA0=sha1("file1.0")
F1SHA1=sha1("file1.1")
F1SHA2=sha1("file1.2")
F2SHA0=sha1("file2.0")
T2SHA1=sha1("file2.1")
CSHA0=sha1("changelog.0")
CSHA1=sha1("changelog.1")
CSHA2=sha1("changelog.2")
CSHA3=sha1("changelog.3")

-- build the cvs repository

cvs_setup()

-- checkout the empty repository and commit some files

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
writefile("testdir/file1", readfile("file1.0"))
writefile("testdir/file2", readfile("file2.0"))
writefile("testdir/changelog", readfile("changelog.0"))
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/file1"), 0, false, false)
check(cvs("add", "testdir/file2"), 0, false, false)
check(cvs("add", "testdir/changelog"), 0, false, false)
check(cvs("commit", "-m", 'initial import', "testdir/file1", "testdir/file2", "testdir/changelog"), 0, false, false)

-- commit first changes
writefile("testdir/file1", readfile("file1.1"))
writefile("testdir/changelog", readfile("changelog.1"))
check(cvs("commit", "-m", 'first commit', "testdir/file1", "testdir/changelog"), 0, false, false)

-- now we create a branch
check(indir("testdir", cvs("tag", "-b", "branched")), 0, false, false)
check(indir("testdir", cvs("up", "-r", "branched")), 0, false, false)

-- alter the files on the branch
writefile("testdir/file2", readfile("file2.1"))
writefile("testdir/changelog", readfile("changelog.3"))
check(cvs("commit", "-m", 'commit on branch', "testdir/file2", "testdir/changelog"), 0, false, false)

-- and create some mainline changes after the branch
check(cvs("up", "-A"), 0, false, false)
writefile("testdir/file1", readfile("file1.2"))
writefile("testdir/changelog", readfile("changelog.2"))
check(cvs("commit", "-m", 'commit on mainline after branch', "testdir/file1", "testdir/changelog"), 0, false, false)

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", cvsroot.."/testdir"), 0, false, false)

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
