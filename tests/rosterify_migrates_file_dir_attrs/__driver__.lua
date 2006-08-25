
mtn_setup()

-- This is a db containing two revisions.  Both contain the files
-- "testfile" and "testdir/otherfile".  The first has a .mt-attrs file
-- that contains:
------
--   file "testfile"
--execute "true"
--
--        file "testdir"
--manual_merge "yes"
--
--   file "nonexistent"
--execute "true"
------
-- The second is the same, except that "yes" and "true" have been
-- swapped in all places.
-- (I know it makes no sense to have manual_merge on a directory, but
-- we want to test attrs on directories, and manual_merge and execute
-- are the only two attrs that get migrated directly...)

remove("test.db")
check(get("test.db.dumped", "stdin"))
check(mtn("db", "load"), 0, false, false, true)

check(mtn("db", "migrate"), 0, false, false)
check(mtn("db", "rosterify"), 0, false, false)

check(mtn("automate", "select", "i:"), 0, true)
check(mtn("automate", "toposort", "-@-"), 0, true, false, {"stdout"})
revs = {}
for l in io.lines("stdout") do
  table.insert(revs, l)
end
-- check the first manifest
check(get("first_manifest_good"))
check(mtn("automate", "get_manifest_of", revs[1]), 0, true)
check(samefile("stdout", "first_manifest_good"))

-- check the second manifest
check(get("second_manifest_good"))
check(mtn("automate", "get_manifest_of", revs[2]), 0, true)
check(samefile("stdout", "second_manifest_good"))
