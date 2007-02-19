
mtn_setup()

addfile("testfile", "blah blah")
check(mtn("commit", "-b", "testbranch", "--date", "2005-08-16T03:16:00", "-m", "foo"), 0, false, false)
R0=base_revision()

writefile("testfile", "stuff stuff")
check(mtn("commit", "-b", "testbranch", "--date", "2005-08-16T03:16:00", "-m", "foo"), 0, false, false)
R1=base_revision()

writefile("testfile", "other other")
check(mtn("commit", "-b", "otherbranch", "--date", "2005-08-16T03:16:05", "-m", "foo"), 0, false, false)
R2=base_revision()

check(mtn("log", "--brief", "--no-graph"), 0, true, false)
check(samelines("stdout", {R2.." tester@test.net 2005-08-16T03:16:05 otherbranch",
                           R1.." tester@test.net 2005-08-16T03:16:00 testbranch",
                           R0.." tester@test.net 2005-08-16T03:16:00 testbranch"}))
