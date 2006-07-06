
mtn_setup()

remove("test.db")

get("test.db.dumped", "stdin")
check(mtn("db", "load"), 0, false, false, true)
check(mtn("db", "migrate"), 0, false, false)

check(mtn("db", "rosterify"), 0, false, false)

check(mtn("db", "rosterify"), 1, false, true)
check(qgrep("already", "stderr"))
