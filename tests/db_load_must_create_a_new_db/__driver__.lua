
mtn_setup()

addfile("foo", "stuff")
commit()

check(mtn("db", "dump"), 0, true, false)
canonicalize("stdout")
rename("stdout", "dump")
copyfile("dump", "stdin")
check(mtn("--db=test2.db", "db", "load"), 0, false, false, true)

mkdir("test3.db")
copyfile("dump", "stdin")
check(mtn("--db=test3.db", "db", "load"), 1, false, false, true)

check(mtn("--db=test4.db", "db", "init"), 0, false, false)
copyfile("dump", "stdin")
check(mtn("--db=test4.db", "db", "load"), 1, false, false, true)
