
mtn_setup()

-- This is a db containing two revisions.  The first revision contains the
-- file "testfile", and has a .mt-attrs file that contains:
------
--   file "testfile"
--    foo "bar"
--execute "yes"
------
-- The second revision is identical, except it also contains the file
-- "otherfile".  The existence of the second revision is purely to work
-- around a current bug in rosterify, in which it breaks for revisions
-- which have no parents and no children.  (It hits an invariant;
-- without the invariant, it would silently throw such revisions away.)

remove("test.db")
check(get("test.db.dumped", "stdin"))
check(mtn("db", "load"), 0, false, false, true)
check(mtn("db", "migrate"), 0, false, false)

check(mtn("db", "rosterify"), 1, false, false)
check(mtn("db", "rosterify", "--drop-attr", "foo"), 0, false, false)

check(mtn("automate", "select", "i:"), 0, true)
check(mtn("automate", "toposort", "-@-"), 0, true, false, {"stdout"})
revs = {}
for l in io.lines("stdout") do
  table.insert(revs, l)
end

check(get("manifest_good"))
check(mtn("automate", "get_manifest_of", revs[1]), 0, true)
check(samefile("stdout", "manifest_good"))
