
mtn_setup()

-- add 'foo/test' file
mkdir("foo")
writefile("foo/test", "test file in foo dir")
check(mtn("add", "foo"), 0, false, false)
commit()

-- rename 'foo' dir to 'bar'
check(mtn("rename", "--bookkeep-only", "foo", "bar"), 0, false, false)
rename("foo", "bar")

-- add new 'foo' dir
mkdir("foo")
writefile("foo/test", "test file in new foo dir")
check(mtn("add", "foo"), 0, false, false)
commit()
