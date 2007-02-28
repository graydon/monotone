
mtn_setup()

addfile("testfile1", "foo")
addfile("testfile2", "bar")
check(mtn("attr", "set", "testfile1", "foo1", "bar"), 0, false, false)
check(mtn("attr", "set", "testfile2", "foo2", "bar"), 0, false, false)
commit()

check(mtn("attr", "set", "testfile1", "foo1", "modified"), 0, false, false)
check(mtn("attr", "set", "testfile1", "bar1", "new-value"), 0, false, false)
check(mtn("attr", "drop", "testfile2", "foo2"), 0, false, false)

check(mtn("status"), 0, true, false)
check(qgrep("testfile1", "stdout"))
check(qgrep("foo1", "stdout"))
check(qgrep("bar1", "stdout"))
check(qgrep("testfile2", "stdout"))
check(qgrep("foo2", "stdout"))
