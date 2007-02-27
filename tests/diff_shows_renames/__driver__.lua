mtn_setup()

mkdir("foo")
addfile("foo/bar", "version 1\n")
commit()
root_rev = base_revision()

check(mtn("mv", "foo/bar", "foo/newbar"), 0, false, false)
writefile("foo/newbar", "version 2!\n")
check(mtn("diff"), 0, true, false)
check(qgrep("^--- foo/bar\t", "stdout"))
check(qgrep("^\\+\\+\\+ foo/newbar\t", "stdout"))

-- The bug here is that the diff code tries to infer renames from the csets
-- involved.  It should just use the pre- and post-roster to get the pre- and
-- post-name.

remove("foo")
revert_to(root_rev)
check(mtn("mv", "foo", "newfoo"), 0, false, false)
writefile("newfoo/bar", "version 2!\n")
check(mtn("diff"), 0, true, false)
xfail(qgrep("^--- foo/bar\t", "stdout"))
check(qgrep("^\\+\\+\\+ newfoo/bar\t", "stdout"))
