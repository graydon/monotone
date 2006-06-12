
mtn_setup()

write_large_file("largish", 1)

check(mtn("genkey", "foo"), 0, false, false, "foo\nfoo\n")
addfile("testfile1", "blah balh")
commit("branch1")
writefile("testfile1", "stuff stuff")
addfile("testfile2", "foo foo")

-- include a largish file in the dump, so we can test for iostream breakage on
-- MinGW wrt sync_with_stdio().
check(mtn("add", "largish"), 0, false, false)
commit("branch2")

-- run a db analyze so that SQLite creates any internal tables and indices,
-- because we want to make sure we don't attempt to dump and load these.
check(mtn("db", "execute", "analyze;"), 0, false, false)

check(mtn("db", "dump"), 0, true, false)
rename("stdout", "stdin")
check(mtn("db", "load", "--db=test2.db"), 0, false, false, true)

check_same_db_contents("test.db", "test2.db")
