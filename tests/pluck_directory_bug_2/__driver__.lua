-- Limitation/bug in pluck: if you pluck a directory, and then you
-- pluck a subsequent revision that adds a file in that directory,
-- you will get a spurious orphaned node conflict.  Reported by
-- Pavel Cahnya <pavel@netbsd.org>.   Variation 2: no commit
-- between the two plucks.

mtn_setup()

-- we need a base revision with something in it
addfile("base", "dummy base file\n")
commit()
root_rev = base_revision()

-- add a directory, a
check(mtn("mkdir", "a"), 0, false, false)
addfile("a/f1", "file for first pluck\n")
commit()
first_rev = base_revision()

-- add a file in that directory
addfile("a/f2", "file for second pluck\n")
commit()
second_rev = base_revision()

revert_to(root_rev)
addfile("b", "some other nonconflicting change\n")
commit()

-- this pluck should give us a/f1 but not a/f2.
check(mtn("pluck", "-r", first_rev), 0, false, false)
check(exists("a/f1"))
check(not exists("a/f2"))

-- this pluck is intended to bring in a/f2, but fails.
xfail(mtn("pluck", "-r", second_rev), 0, false, false)
