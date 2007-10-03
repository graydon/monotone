
mtn_setup()

check(get("cvs-repository"))

-- the contents from which we created the test module
writefile("cvsfile", "this is a line of text.\n")

tsha = sha1("cvsfile")

-- import into monotone and check presence of files

check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("automate", "get_file", tsha), 0, false)
