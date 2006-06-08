
skip_if(not existsonpath("cvs"))
mtn_setup()

cvsroot = test.root.."/cvs-repository"
function cvs(...)
  return {"cvs", "-d", cvsroot, unpack(arg)}
end

check(cvs("-q", "init"), 0, false, false)
check(exists(cvsroot))
check(exists(cvsroot.."/CVSROOT"))
check(exists(cvsroot.."/CVSROOT/modules"))

mkdir(cvsroot.."/attest")
getfile("cvsfile,v", cvsroot.."/attest/cvsfile,v")

check(cvs("co", "-rABC_BASE", "attest"), 0, false, false)
tsha = sha1("attest/cvsfile")

-- import into monotone and check presence of files

check(mtn("--branch=testbranch", "cvs_import", cvsroot.."/attest"), 0, false, false)

check(mtn("automate", "get_file", tsha), 0, false)
