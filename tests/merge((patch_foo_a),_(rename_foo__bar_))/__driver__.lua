
mtn_setup()

mkdir("foo")
addfile("foo/a", "blah blah")
commit()
base = base_revision()

rename("foo", "bar")
check(mtn("rename", "foo", "bar"), 0, false, false)
commit()

rename("bar", "foo")
revert_to(base)
writefile("foo/a", "some other stuff")
commit()

check(mtn("--branch=testbranch", "merge"), 0, false, false)

check(mtn("checkout", "--revision", base, "test_dir"), 0, false, false)
check(indir("test_dir", mtn("update", "--branch=testbranch")), 0, false, false)

check(not exists("test_dir/foo/a"))
check(exists("test_dir/bar/a"))
check(samefile("foo/a", "test_dir/bar/a"))
