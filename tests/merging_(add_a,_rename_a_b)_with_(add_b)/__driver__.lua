
mtn_setup()

--              A
--            /   \
--           B     C
--            \   /
--              D
--
-- A is the common ancestor, containing 'add foo'.  B contains 'rename foo
-- bar'.  C contains 'add bar'.  D is a conflict.
revs = {}

writefile("foo", "extra blah blah foo")

-- produce state A
check(mtn("add", "foo"), 0, false, false)
commit()
revs.a = base_revision()

-- produce state B
check(mtn("rename", "--bookkeep-only", "foo", "bar"), 0, false, false)
rename("foo", "bar")
commit()
revs.b = base_revision()

-- produce state C
revert_to(revs.a)
writefile("bar", "extra blah blah foo")
check(mtn("add", "bar"), 0, false, false)
commit()
revs.c = base_revision()

-- merge heads to make D
-- this is a conflict
check(mtn("merge"), 1, false, false)
