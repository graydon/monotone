
mtn_setup()

-- check that db info of a new database works

check(mtn("db", "info"), 0, false, false)
