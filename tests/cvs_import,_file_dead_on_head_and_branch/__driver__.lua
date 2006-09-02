
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

mkdir(cvsroot.."/attest")
check(get("cvsfile,v", cvsroot.."/attest/cvsfile,v"))

check(cvs("co", "-rABC_BASE", "attest"), 0, false, false)
tsha = sha1("attest/cvsfile")

-- import into monotone and check presence of files

check(mtn("--branch=testbranch", "cvs_import", cvsroot.."/attest"), 0, false, false)

check(mtn("automate", "get_file", tsha), 0, false)
