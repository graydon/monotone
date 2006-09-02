
mtn_setup()

-- This is a db containing only one revision.  The revision contains
-- two files; "testfile" and "testdir/otherfile".

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

check(get("manifest_good"))
check(mtn("automate", "get_manifest_of", revs[1]), 0, true)
check(samefile("stdout", "manifest_good"))
