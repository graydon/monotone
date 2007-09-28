skip_if(ostype == "Windows")
-- FIXME: this test broke on Windows when the change to running tests
-- in parallel was made. There appears to be a race condition in
-- waiting for the spawned test process; running under the debugger
-- with breaks after the spawn but before the wait makes the test work.
--
-- FIXME: on top of that, the Lua mechanisms can't handle the error;
-- win32/tester-plaf.cc run_tests_in_children returns status -1 to
-- test_cleaner; that eventually gets passed to ldo.cc
-- luaD_seterrorobj, which doesn't handle a status of -1, and
-- tester.exe just exits. So xfail won't work here either.

mtn_setup()

check(get("testhooks"))

check(raw_mtn("--rcfile=testhooks", "ls", "unknown"), 0, false, false)
skip_if(exists("skipfile"))
check(exists("outfile"))
