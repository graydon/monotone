
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create the initial 3rd-party vendor import

mkdir("importdir")
writefile("file1.0", "version 0 of test file1")
copy("file1.0", "importdir/file1")
writefile("file2.0", "version 0 of test file2")
copy("file2.0", "importdir/file2")
writefile("changelog.0", "first changelog entry\n")
copy("changelog.0", "importdir/changelog")
check(indir("importdir", cvs("import", "-m", "Initial import of VENDORWARE 1", "testsrc", "VENDOR", "VENDOR_REL_1")), 0, false, false)

-- now we alter some of the files.
check(cvs("co", "testsrc"), 0, false, false)
writefile("file1.1", "version 1 of test file1")
copy("file1.1", "testsrc/file1")
check(cat("-", "changelog.0"), 0, true, false, "second changelog\n\n")
rename("stdout", "changelog.1")
copy("changelog.1", "testsrc/changelog")
check(indir("testsrc", cvs ("commit", "-m", 'commit 0')), 0, false, false)

check(cat("-", "changelog.1"), 0, true, false, "third changelog -not on branch-\n\n")
rename("stdout", "changelog.2")
-- now we create a branch
check(indir("testsrc", cvs ("tag", "-b", "branched")), 0, false, false)
check(indir("testsrc", cvs ("up", "-r", "branched")), 0, false, false)

-- alter the files on the branch
writefile("file1.2", "version 2 of test file1")
copy("file1.2", "testsrc/file1")
writefile("file2.1", "version 1 of test file2")
copy("file2.1", "testsrc/file2")
check(cat("-", "changelog.1"), 0, true, false, "third changelog -on branch-\n\n")
rename("stdout", "changelog.3")
copy("changelog.3", "testsrc/changelog")
check(indir("testsrc", cvs ("commit", "-m", 'commit on branch')), 0, false, false)

-- and create some mainline changes after the branch
check(indir("testsrc", cvs ("up", "-A")), 0, false, false)
copy("changelog.2", "testsrc/changelog")
check(indir("testsrc", cvs ("commit", "-m", 'commit on mainline after branch')), 0, false, false)

-- import into monotone and check presence of files

check(mtn("--branch=test", "cvs_import", cvsroot.."/testsrc"), 0, false, false)

-- also check that checkout is correct
-- right one.

check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.branched", "branchdir"), 0, false, false)

check(samefile("file1.1", "maindir/file1"))
check(samefile("file2.0", "maindir/file2"))
check(samefile("changelog.2", "maindir/changelog"))

check(samefile("file1.2", "branchdir/file1"))
check(samefile("file2.1", "branchdir/file2"))
check(samefile("changelog.3", "branchdir/changelog"))
