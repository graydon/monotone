
mtn_setup()

writefile("base", "foo blah")
writefile("left", "bar blah")

copy("base", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

copy("left", "testfile")
commit()
left = base_revision()

revert_to(base)

check(mtn("drop", "testfile"), 0, false, false)
commit()

check(mtn("merge"), 0, false, true)

-- check that we're warned about the changes being dropped...

check(qgrep("Content changes to the file", "stderr"))
check(qgrep(left, "stderr"))

