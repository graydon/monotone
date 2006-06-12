
skip_if(not existsonpath("env"))
mtn_setup()

function noenv_mtn(...)
  return {"env", "-i", unpack(mtn(unpack(arg)))}
end

if os.getenv("OSTYPE") == "msys" then
  local iconv = getpathof("libiconv-2", ".dll")
  local zlib = getpathof("zlib1", ".dll")
  copyfile(iconv, "libiconv-2.dll")
  copyfile(zlib, "zlib1.dll")
end

check(noenv_mtn("--help"), 2, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
