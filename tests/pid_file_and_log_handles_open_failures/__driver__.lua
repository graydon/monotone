
skip_if(not existsonpath("chmod"))
mtn_setup()

-- --log should fail if it can't open the file.
writefile("inaccessible.log", "")
check({"chmod", "000", "inaccessible.log"})
check(mtn("--log=inaccessible.log", "status"), 1, false, false)

-- and it should fail if the specified file is read only.
writefile("ro.log", "")
check({"chmod", "400", "ro.log"})
check(mtn("--log=ro.log", "status"), 1, false, false)

mkdir("noaccess")
-- skip part of the test on win32 for now as the permission restrictions
-- don't map to the NT permissions we need.
if ostype == "Windows" then
  test.partial_skip = true
else
  -- it should also fail if a parent directory of the file is not accessible.
  check({"chmod", "100", "noaccess"})
  check(mtn("--log=noaccess/my.log", "status"), 1, false, false)

  srv = bg(mtn("serve", "--bind=127.0.0.2:55597", "--pid-file=noaccess/my.pid"), 1, false, false)
  
  check(srv:wait(5))
end

remove("noaccess")
remove("ro.log")
remove("inaccessible.log")
