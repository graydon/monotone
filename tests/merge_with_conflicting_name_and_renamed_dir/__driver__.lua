
mtn_setup()

-- based on a bug report by Marcin W. Dąbrowski, simplified by
-- Markus Schiltknecht, leaving away the branches names, just plain
-- merges.
--
--
--           base_rev              o  contains directory "foo"
--          /        \
--         /          \
--    adds file     adds file      o  conflicting file additions
--    "foo/x"         "foo/x"         on both sides
--    and               |
--    renames           |          o  renaming parent directory
--    dir "foo"         |             on one side
--    to "bar"          |
--        \            /
--         \          /
--          ' !BOOM! '             o  merge should fail due to
--                                    duplicate names
--
-- The merge currently failes with:
-- ../nvm/roster_merge.cc:528: invariant 'I(left_name == right_name)' violated

revs = {}

mkdir("foo")
adddir("foo")
commit()
base_rev = base_revision()

-- rename that directory
addfile("foo/x", "some contents")
check(mtn("mv", "foo", "bar"), 0, false, false)
commit()

-- try merging
check(mtn("merge"), 0, false, false)

-- revert, and add a file in that directory
revert_to(base_rev)
addfile("foo/x", "conflicting contents")
commit()

-- try merging
xfail(mtn("merge"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

