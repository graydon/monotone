mtn_setup()

mkdir("foo")
addfile("foo/bar", "version 1\n")
commit()
root_rev = base_revision()

check(mtn("mv", "-e", "foo/bar", "foo/newbar"), 0, false, false)
writefile("foo/newbar", "version 2!\n")
check(mtn("diff"), 0, true, false)
check(qgrep("^--- foo/bar\t", "stdout"))
check(qgrep("^\\+\\+\\+ foo/newbar\t", "stdout"))

revert_to(root_rev)
check(mtn("mv", "-e", "foo", "newfoo"), 0, false, false)
writefile("newfoo/bar", "version 2!\n")
check(mtn("diff"), 0, true, false)
xfail(qgrep("^--- foo/bar\t", "stdout"))
check(qgrep("^\\+\\+\\+ newfoo/bar\t", "stdout"))
