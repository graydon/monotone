
include("/common/cvs.lua")
mtn_setup()

cvs_setup()

check(get("attest", cvsroot.."/attest"))

check(cvs("co", "-rABC_BASE", "attest"), 0, false, false)
tsha0 = sha1("attest/afile")
tsha1 = sha1("attest/bfile")
tsha2 = sha1("attest/cfile")

-- import into monotone and check presence of files

check(mtn("--branch=testbranch", "cvs_import", cvsroot.."/attest"), 0, false, false)

check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
