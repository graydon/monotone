
mtn_setup()

-- this test just tests that the testsuite macro CHECK_SAME_DB_CONTENTS works,
-- because other tests depend on that.

addfile("testfile", "blah")
commit("testbranch1")

addfile("testfile2", "foo")
commit("testbranch2")

copy("test.db", "test2.db")
check_same_db_contents("test.db", "test2.db")

addfile("testfile3", "pizza")
commit()

check(not pcall(check_same_db_contents, "test.db", "test2.db"))
