
if existsonpath("cp") then
  getfile("nix-cphook.lua", "cphook.lua")
elseif existsonpath("xcopy") then
  getfile("win-cphook.lua", "cphook.lua")
else
  skip_if(true)
end

mtn_setup()

writefile("testfile", "blah blah")

check(mtn("add", "cphook.lua"), 0, false, false)
check(mtn("--branch=testbranch", "--rcfile=cphook.lua", "commit", "--message=test"), 0, false, false)
check(exists("testfile.copied"))
