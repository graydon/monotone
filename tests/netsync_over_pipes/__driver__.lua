
mtn_setup()

copy("test.db", "test2.db")

addfile("testfile", "foo")
commit()

check(mtn("sync", "file:test2.db", "testbranch"), 0, false, false)
check_same_db_contents("test.db", "test2.db")
