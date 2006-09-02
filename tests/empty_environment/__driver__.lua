
skip_if(not existsonpath("env"))
mtn_setup()

function noenv_mtn(...)
  return {"env", "-i", unpack(mtn(unpack(arg)))}
end

if os.getenv("OSTYPE") == "msys" then
  local iconv = getpathof("libiconv-2", ".dll")
  local zlib = getpathof("zlib1", ".dll")
  copy(iconv, "libiconv-2.dll")
  copy(zlib, "zlib1.dll")
elseif os.getenv("OSTYPE") == "cygwin" then
  local cygwin = getpathof("cygwin1", ".dll")
  local iconv = getpathof("cygiconv-2", ".dll")
  local intl = getpathof("cygintl-3", ".dll")
  local zlib = getpathof("cygz", ".dll")
  copy(cygwin, "cygwin1.dll")
  copy(iconv, "cygiconv-2.dll")
  copy(intl, "cygintl-3.dll")
  copy(zlib, "cygz.dll")
end

check(noenv_mtn("--help"), 0, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
