
skip_if(ostype == "Windows")
mtn_setup()

-- SIGTERM and SIGINT can't really be handled on Win32:
-- http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vclib/html/_CRT_signal.asp
-- We also don't attempt to install handlers for them in main.cc, but there's
-- little point given the discussion at the MSDN link above.

-- this test checks that .db-journal files aren't left lying about if the 
-- process is killed with SIGTERM or SIGINT

writefile("testfile", "stuff")
check(mtn("add", "testfile"), 0, false, false)

-- a hack to make the monotone process hang around with the database locked.

writefile("wait.lua", "function get_passphrase(key) sleep(1000) end")


-- SIGTERM first
process = bg(mtn("--rcfile=wait.lua", "-btestbranch", "ci", "-mx"), false, false, false)
sleep(2)
check(exists("test.db-journal"))
kill(process.pid, 15)
retval, res = timed_wait(process.pid, 2)
check(res == 0)
check(retval == -15) -- signal, not exit
xfail_if(exists("test.db-journal"))

-- and again for SIGINT
process = bg(mtn("--rcfile=wait.lua", "-btestbranch", "ci", "-mx"), false, false, false)
sleep(2)
check(exists("test.db-journal"))
kill(process.pid, 2)
retval, res = timed_wait(process.pid, 2)
check(res == 0)
check(retval == -1) -- signal, not exit
xfail_if(exists("test.db-journal"))

-- should *not* be cleaned up for SIGSEGV
process = bg(mtn("--rcfile=wait.lua", "-btestbranch", "ci", "-mx"), false, false, false)
sleep(2)
check(exists("test.db-journal"))
kill(process.pid, 11)
retval, res = timed_wait(process.pid, 2)
check(res == 0)
check(retval == -11) -- signal, not exit
check(exists("test.db-journal"))
