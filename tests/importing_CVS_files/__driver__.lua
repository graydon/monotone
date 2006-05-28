
mtn_setup()

skip_if(not existsonpath("cvs"))

writefile("importme.0", "version 0 of test file")

writefile("importme.1", "version 1 of test file")

writefile("importme.2", "version 2 of test file")

writefile("importme.3", "version 3 of test file")

tsha0=sha1("importme.0")
tsha1=sha1("importme.1")
tsha2=sha1("importme.2")
tsha3=sha1("importme.3")

-- build the cvs repository

cvsroot = test_root .. "/cvs-repository"
check(cmd("cvs", "-q", "-d", cvsroot, "init"), 0, false, false)
check(exists(cvsroot))
check(exists(cvsroot .. "/CVSROOT"))
check(exists(cvsroot .. "/CVSROOT/modules"))

-- check out the workspace and make some commits
-- note that this has to use copyfile, rather than rename, to update
-- the file in cvs. Apparently, cvs uses timestamps or something to track
-- file modifications.
check(cmd("cvs", "-d", cvsroot, "co", "."), 0, false, false)
mkdir("testdir")
copyfile("importme.0", "testdir/importme")
check(cmd("cvs", "-d", cvsroot, "add", "testdir"), 0, false, false)
check(cmd("cvs", "-d", cvsroot, "add", "testdir/importme"), 0, false, false)
check(cmd("cvs", "-d", cvsroot, "commit", "-m", 'commit 0', "testdir/importme"), 0, false, false)
copyfile("importme.1", "testdir/importme")
check(cmd("cvs", "-d", cvsroot, "commit", "-m", 'commit 1', "testdir/importme"), 0, false, false)
copyfile("importme.2", "testdir/importme")
check(cmd("cvs", "-d", cvsroot, "commit", "-m", 'commit 2', "testdir/importme"), 0, false, false)
copyfile("importme.3", "testdir/importme")
check(cmd("cvs", "-d", cvsroot, "commit", "-m", 'commit 3', "testdir/importme"), 0, false, false)

-- import into monotone and check presence of files

-- safety check -- we stop people from accidentally feeding their whole
-- repo to cvs_import instead of just a module.
check(cmd(mtn("--branch=testbranch", "cvs_import", cvsroot)), 1, false, false)
check(cmd(mtn("--branch=testbranch", "cvs_import", cvsroot .. "/testdir")), 0, false, false)
check(cmd(mtn("automate", "get_file", tsha0)), 0, false)
check(cmd(mtn("automate", "get_file", tsha1)), 0, false)
check(cmd(mtn("automate", "get_file", tsha2)), 0, false)
check(cmd(mtn("automate", "get_file", tsha3)), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.

check(cmd(mtn("checkout", "--branch=testbranch", "mtcodir")), 0, false, false)
check(samefile("importme.3", "mtcodir/importme"))
