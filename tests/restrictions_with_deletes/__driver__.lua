mtn_setup()

-- This test is designed to tickle some bugs in the restrictions code.  In
-- particular, we want to prevent the case where the deletion of foo is
-- included in a cset, but the deletion of foo/bar is not -- that would 
-- result in a nonsense cset.  However, the current code only knows about 
-- adds, not about deletes or renames.

-- The fix for this bug is to make check_restricted_cset use cs.apply_to with
-- a special editable_tree that nicely detects invalid attach_node calls.
-- Also, possibly 'diff' should just work even when given a funky restriction,
-- rather than forcing people to mention more stuff just so they can see what
-- they've done... if so, this test should be changed a bit.

-- ways to need a path: add, rename
-- ways to lose a path: drop, rename

mkdir("foo")
addfile("foo/bar", "foobar")
commit()
root_rev = base_revision()

check(mtn("drop", "--bookkeep-only", "foo/bar", "foo"), 0, false, false)
check(mtn("st", "--exclude", "foo/bar"), 1, false, false)
