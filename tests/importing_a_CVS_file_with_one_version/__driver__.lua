
skip_if(not existsonpath("cvs"))
mtn_setup()

writefile("importme.0", "version 0 of test file")

tsha = sha1("importme.0")

-- build the cvs repository

cvsroot = test_root .. "/cvs-repository"
check(cmd("cvs", "-q", "-d", cvsroot, "init"), 0, false, false)
check(exists(cvsroot))
check(exists(cvsroot .. "/CVSROOT"))
check(exists(cvsroot .. "/CVSROOT/modules"))

-- check out the workspace and make a commit

check(cmd("cvs", "-d", cvsroot, "co", "."), 0, false, false)
mkdir("testdir")
os.rename("importme.0", "testdir/importme")
check(cmd("cvs", "-d", cvsroot, "add", "testdir"), 0, false, false)
check(cmd("cvs", "-d", cvsroot, "add", "testdir/importme"), 0, false, false)
check(cmd("cvs", "-d", cvsroot, "commit", "-m", 'commit 0', "testdir/importme"), 0, false, false)

-- import into monotone and check presence of file

check(cmd(mtn("--branch=testbranch", "cvs_import", cvsroot .. "/testdir")), 0, false, false)
check(cmd(mtn("automate", "get_file", tsha)), 0, false)
