
mtn_setup()

-- testcase for bug --15877

addfile("testfile", "blah blah")
check(mtn("commit", "-b", "testbranch", "--date=2005-08-16T03:16:00", "-m", "foo"), 0, false, false)
R0=base_revision()

writefile("testfile", "stuff stuff")
check(mtn("commit", "-b", "testbranch", "--date=2005-08-16T03:16:00", "-m", "foo"), 0, false, false)
R1=base_revision()

writefile("testfile", "other other")
check(mtn("commit", "-b", "testbranch", "--date=2005-08-16T03:16:05", "-m", "foo"), 0, false, false)
R2=base_revision()

check(raw_mtn("--db", test.root.."/test.db", "--root", test.root,
              "log", "--brief", "--from", "d:2005-08-16"), 0, true, false)
check(qgrep(R0, "stdout"))
check(qgrep(R1, "stdout"))
check(qgrep(R2, "stdout"))
