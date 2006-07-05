
skip_if(ostype == "Windows")
skip_if(not existsonpath("chmod"))
mtn_setup()

addfile("testfile", "blah blah")

-- unreadable file
check({"chmod", "a-rwx", "test.db"})
check(mtn(), 2, false, false)
check(mtn("ls", "branches"), 1, false, false)
check(mtn("db", "info"), 1, false, false)
check(mtn("db", "version"), 1, false, false)
check(mtn("db", "migrate"), 1, false, false)
check(mtn("commit", "-mfoo"), 1, false, false)
check(mtn("db", "load"), 1, false, false)
check({"chmod", "a+rwx", "test.db"})

mkdir("subdir")
check(mtn("--db=subdir/foo.db", "db", "init"), 0, false, false)

-- unreadable directory
check({"chmod", "a-rwx", "subdir"})
check(mtn("--db=subdir/foo.db"), 2, false, false)
check(mtn("--db=subdir/foo.db", "ls", "branches"), 1, false, false)
check(mtn("--db=subdir/foo.db", "db", "info"), 1, false, false)
check(mtn("--db=subdir/foo.db", "db", "version"), 1, false, false)
check(mtn("--db=subdir/foo.db", "db", "migrate"), 1, false, false)
check(mtn("--db=subdir/foo.db", "db", "load"), 1, false, false)
check(mtn("--db=subdir/bar.db", "db", "init"), 1, false, false)
check({"chmod", "a+rwx", "subdir"})
