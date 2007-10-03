
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

writefile("foo.0", "version 0 of test file foo\n")
writefile("foo.1", "version 1 of test file foo\n")
writefile("foo.2", "version 2 of test file foo\n")
writefile("foo.3", "version 3 of test file foo\n")
tsha0 = sha1("foo.0")
tsha1 = sha1("foo.1")
tsha2 = sha1("foo.2")
tsha3 = sha1("foo.3")

-- safety check -- we stop people from accidentally feeding their whole
-- repo to cvs_import instead of just a module. For that, we re-add the
-- CVSROOT bookkeeping directory
mkdir("cvs-repository/CVSROOT")
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository"), 1, false, false)

-- import into monotone and check presence of files
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)
check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
check(mtn("automate", "get_file", tsha3), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(samefile("foo.3", "mtcodir/foo"))
