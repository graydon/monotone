-- -*-lua-*-

mtn_setup()
revs = {}

addfile("foo", "a\nb\nc\n")
writefile("foo.rightnewname", "a\nb\nx\n")

commit()
revs.base = base_revision()


-- do something to a file we don't care about to make a commitable change
addfile("junk", "some junk")
commit()
revs.left = base_revision()

revert_to(revs.base)
remove("junk")

copy("foo.rightnewname", "foo")
check(mtn("rename", "foo", "foo.new"), 0, false, false)
commit()
revs.right = base_revision()


check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.merged = base_revision()

--
-- annotate foo should now be
-- base: a
-- base: b
-- right: x
--

check(mtn("annotate", "--revs-only", "foo.new"), 0, true, false)
check(greplines("stdout", {revs.base, revs.base, revs.right}))
