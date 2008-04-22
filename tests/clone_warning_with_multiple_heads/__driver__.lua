skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("testfile", "foo")
commit()
REV1=base_revision()

addfile("file2", "bar")
commit()
REV2=base_revision()

check(mtn("update", "-r", REV1), 0, false, false)
addfile("otherfile", "splork")
commit()
REV3=base_revision()

testURI="file:" .. test.root .. "/test.db"

check(nodb_mtn("--branch=testbranch", "clone", testURI, "test_dirA"),
         1, false, true)
check(qgrep(REV2, "stderr"))
check(qgrep(REV3, "stderr"))

on_windows=(ostype == "Windows")
on_solaris=(string.find(ostype, 'SunOS') ~= nil)
bad_platform=(on_solaris or on_windows)
xfail_if(bad_platform, not exists("test_dirA"))
