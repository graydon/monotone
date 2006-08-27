
mtn_setup()

remove("test.db")

check(get("test.db.dumped", "stdin"))
check(mtn("db", "load"), 0, false, false, true)
check(mtn("db", "migrate"), 0, false, false)
check(mtn("db", "rosterify"), 0, false, false)

found_del = false
found_add = false
check(mtn("automate", "select", "i:"), 0, true)
rename("stdout", "revs")
for r in io.lines("revs") do
  check(mtn("automate", "get_revision", r), 0, true)
  if qgrep('delete ""', "stdout") then found_del = true end
  if qgrep('add_dir ""', "stdout") then found_add = true end
end

check(found_del and found_add)
