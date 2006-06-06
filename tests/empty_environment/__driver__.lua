
skip_if(not existsonpath("env"))
mtn_setup()

function noenv_mtn(...)
  return {"env", "-i", unpack(mtn(unpack(arg)))}
end

--check(if test "$OSTYPE" = "msys"; then
--  cp $(which libiconv-2.dll) .
--  cp $(which zlib1.dll) .
--fi)

check(noenv_mtn("--help"), 2, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--branch=testbranch", "--message=foo"), 0, false, false)

check(false) -- need to fix for windows
