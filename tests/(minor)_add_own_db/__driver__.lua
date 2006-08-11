
mtn_setup()

--Adding the current in-use DB should fail
-- <Sircus> ...It *really* seems like adding the db to the db is something that should be caught and an error message thrown...
-- <Sircus> (What's revert supposed to do when you revert the db? :-)
-- ...
-- <njs> and add a note in a comment that "revert" is the case that really moves this 
-- from being "stupid user tricks" to "something we should protect users from" :-)
check(mtn("add", "test.db"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(not qgrep("'test.db'", "stdout"))

check(mtn("add", "."), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(not qgrep("'test.db'", "stdout"))

mkdir("subdir")
copy("test.db", "subdir/test.db")

check(mtn("--db=subdir/test.db", "add", "subdir/test.db"), 0, false, false)
check(mtn("--db=subdir/test.db", "ls", "known"), 0, true, false)
check(not qgrep("'subdir/test.db'", "stdout"))

-- If it's not an in-use DB, it should work, though
check(mtn("add", "subdir/test.db"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("subdir/test.db", "stdout"))
