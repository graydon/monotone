
skip_if(not existsonpath("cvs"))
mtn_setup()

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
function cvs(...)
  return {"cvs", "-d", cvsroot, unpack(arg)}
end
check(cvs("-q", "init"), 0, false, false)
check(exists(cvsroot))
check(exists(cvsroot.."/CVSROOT"))
check(exists(cvsroot.."/CVSROOT/modules"))

-- check out the workspace and make some commits

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
copyfile("importme.0", "testdir/importme")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/importme"), 0, false, false)
check(cvs("commit", "-m", 'commit 0', "testdir/importme"), 0, false, false)
copyfile("importme.1", "testdir/importme")
check(cvs("commit", "-m", 'commit same message', "testdir/importme"), 0, false, false)
copyfile("importme.2", "testdir/importme")
check(cvs("commit", "-m", 'commit same message', "testdir/importme"), 0, false, false)
copyfile("importme.3", "testdir/importme")
check(cvs("commit", "-m", 'commit 3', "testdir/importme"), 0, false, false)

-- import into monotone and check presence of files

check(mtn("--branch=testbranch", "cvs_import", cvsroot.."/testdir"), 0, false, false)
check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
check(mtn("automate", "get_file", tsha3), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.

check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(samefile("importme.3", "mtcodir/importme"))
