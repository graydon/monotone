
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))


writefile("foo", "version 0 of test file foo\n")
tsha = sha1("foo")

check(mtn("--branch=testbranch", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("automate", "get_file", tsha), 0, false)
