
mtn_setup()

check(get("cvs-repository"))

-- the contents from which we created the test module
writefile("afile", "this is a line of text.\n")
writefile("bfile", "this is some text over here.\n")
writefile("cfile", "what's this? it's a line of text, sir.\n")

tsha0 = sha1("afile")
tsha1 = sha1("bfile")
tsha2 = sha1("cfile")

-- import into monotone and check presence of files
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
