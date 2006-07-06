
if existsonpath("cp") then
  get("nix-cphook.lua", "cphook.lua")
elseif existsonpath("xcopy") then
  -- This actually copies it to testfile.copied/testfile,
  -- but that still makes testfile.copied exists. So it's
  -- not exactly the same as the other hook, but it works.
  get("win-cphook.lua", "cphook.lua")
else
  skip_if(true)
end

mtn_setup()

writefile("testfile", "blah blah")

check(mtn("add", "cphook.lua"), 0, false, false)
check(mtn("--branch=testbranch", "--rcfile=cphook.lua", "commit", "--message=test"), 0, false, false)
check(exists("testfile.copied"))
