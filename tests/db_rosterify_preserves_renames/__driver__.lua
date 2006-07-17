
mtn_setup()
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

-- check the second revision
check(get("expout"))
check(mtn("automate", "get_revision", revs[2]), 0, true)
check(samefile("stdout", "expout"))
