
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

  -- we use --stdio here to avoid the possibility of the pidfile being
  -- created and then the test tripping over network restrictions.
  srv = bg(mtn("serve", "--stdio", "--pid-file=noaccess/my.pid"),
	   1, nil, true, nil)

  check(srv:wait(5))
  check(qgrep("failed to create pid file", "stderr"))
end

remove("noaccess")
remove("ro.log")
remove("inaccessible.log")
