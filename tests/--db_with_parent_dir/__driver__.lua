
mtn_setup()

mkdir("foo")
chdir("foo")
check(mtn("--db=../new.db", "db", "init"), 0, false, false)
check(mtn("--db=../new.db", "ls", "branches"), 0, false, false)
chdir("..")

-- paths in _MTN/options should be absolute and not contain ..

mkdir("bar")
chdir("bar")
check(mtn("--db=../new.db", "--branch=testbranch", "setup", "."), 0, false, false)
chdir("..")
check(grep("new.db", "bar/_MTN/options"), 0, true)
check(grep("-v", "\\.\\.", "stdout"), 0, false, false)
