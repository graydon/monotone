
mtn_setup()

mkdir("foo")
writefile("foo/bar", "foo bar file")

-- common ancestor

check(mtn("add", "foo/bar"), 0, false, false)
commit()
base = base_revision()

-- rename directory

check(mtn("rename", "foo", "foof"), 0, false, false)
commit()

-- rename file in directory

revert_to(base)
check(mtn("rename", "foo/bar", "foo/barf"), 0, false, false)
commit()

-- merge heads to arrive at foof/barf

check(mtn("merge"), 0, false, false)
check(mtn("co", "dir"), 0, false, false)
check(exists("dir/foof"))
check(exists("dir/foof/barf"))
