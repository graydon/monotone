mtn_setup()

writefile("empty", "")
writefile("expected", "testvalue")

check(mtn("automate", "db_set", "testdomain", "testname", "testvalue"), 0, true, false)
check(samefile("empty", "stdout"))

check(mtn("automate", "db_get", "testdomain", "testname"), 0, true, false)
check(samefile("expected", "stdout"))

-- ensure that missing names fail
check(mtn("automate", "db_get", "testdomain", "testname2"), 1, true, false)
