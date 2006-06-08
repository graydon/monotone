
skip_if(not existsonpath("cvs"))
mtn_setup()

writefile("importme.0", "version 0 of test file")

tsha = sha1("importme.0")

-- build the cvs repository

cvsroot = test.root .. "/cvs-repository"

function cvs(...)
  return {"cvs", "-d", cvsroot, unpack(arg)}
end

check(cvs("-q", "init"), 0, false, false)
check(exists(cvsroot))
check(exists(cvsroot .. "/CVSROOT"))
check(exists(cvsroot .. "/CVSROOT/modules"))

-- check out the workspace and make a commit

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
os.rename("importme.0", "testdir/importme")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/importme"), 0, false, false)
check(cvs("commit", "-m", 'commit 0', "testdir/importme"), 0, false, false)

-- import into monotone and check presence of file

check(mtn("--branch=testbranch", "cvs_import", cvsroot .. "/testdir"), 0, false, false)
check(mtn("automate", "get_file", tsha), 0, false)
