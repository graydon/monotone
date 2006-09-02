
mtn_setup()

writefile("foo", "foo")
writefile("bar", "bar")

-- the first db has a single rev, containing a file ".mt-ignore", with
-- contents "foo".  rosterify should turn it into .mtn-ignore.

remove("test.db")
check(get("ignore-1.db.dump", "stdin"))
check(mtn("db", "load"), 0, false, false, true)
check(mtn("db", "migrate"), 0, false, false)
check(mtn("db", "rosterify"), 0, false, false)

check(mtn("co", "-b", "testbranch", "codir-1"), 0, false, false)
check(exists("codir-1/.mtn-ignore"))
check(trim(readfile("codir-1/.mtn-ignore")) == "foo")
check(not exists("codir-1/.mt-ignore"))

-- the second db has a single rev, containing a file ".mt-ignore" with
-- contents "foo", and a file ".mtn-ignore" with contents "bar".
-- rosterify should leave them both alone.
remove("test.db")
check(get("ignore-2.db.dump", "stdin"))
check(mtn("db", "load"), 0, false, false, true)

check(mtn("db", "migrate"), 0, false, false)
check(mtn("db", "rosterify"), 0, false, false)

check(mtn("co", "-b", "testbranch", "codir-2"), 0, false, false)
check(exists("codir-2/.mtn-ignore"))
check(trim(readfile("codir-2/.mtn-ignore")) == "bar")
check(exists("codir-2/.mt-ignore"))
check(trim(readfile("codir-2/.mt-ignore")) == "foo")
