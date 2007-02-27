mtn_setup()

-- This test is designed to tickle some bugs in the restrictions code.  In
-- particular, we want to prevent the case where the addition of foo/bar is
-- included in a cset, but the addition of foo/ is not -- that would result in
-- a nonsense cset.  However, the current code only knows about adds, not
-- about deletes or renames.

-- The fix for this bug is to make check_restricted_cset use cs.apply_to with
-- a special editable_tree that nicely detects invalid attach_node calls.
-- Also, possibly 'diff' should just work even when given a funky restriction,
-- rather than forcing people to mention more stuff just so they can see what
-- they've done... if so, this test should be changed a bit.

-- ways to need a path: add, rename
-- ways to lose a path: drop, rename

addfile("whatever", "balsdfas")
commit()
root_rev = base_revision()

-- easiest way to get: rename a dir, and add something to it, and then diff
mkdir("testdir")
check(mtn("add", "testdir"), 0, false, false)
commit()
rev_with_testdir = base_revision()

check(mtn("mv", "testdir", "newdir"), 0, false, false)
addfile("newdir/foo", "blah blah\n")
-- these should succeed.
check(mtn("diff"), 0, false, false)
check(mtn("commit", "-m", "foo"), 0, false, false)

-- or: rename a dir A, add a replacement B, add something C to the
--   replacement, then use a restriction that includes A and C only
check(mtn("up", "-r", rev_with_testdir), 0, false, false)
check(not exists("newdir"))
check(mtn("mv", "testdir", "newdir"), 0, false, false)
mkdir("testdir")
addfile("testdir/newfile", "asdfasdf")
-- these are nonsensical, and should error out gracefully
check(mtn("diff", "newdir", "testdir/newfile"), 1, false, false)
check(mtn("commit", "newdir", "testdir/newfile"), 1, false, false)

-- or: rename A, then rename B under it, and use a restriction that includes
--   only B
check(mtn("up", "-r", root_rev), 0, false, false)
mkdir("A")
mkdir("B")
check(mtn("add", "A", "B"), 0, false, false)
commit()
check(mtn("rename", "A", "newA"), 0, false, false)
check(mtn("rename", "B", "newA/B"), 0, false, false)
-- these are nonsensical, and should error out gracefully
check(mtn("diff", "newA/B"), 1, false, false)
check(mtn("commit", "newA/B"), 1, false, false)
