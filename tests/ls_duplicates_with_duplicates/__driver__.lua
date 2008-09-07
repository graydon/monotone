
mtn_setup()

check(mtn("ls", "duplicates", "bla"), 2, false, false);

addfile("unique", "unique")
commit()

addfile("testfile", "blah blah")
commit()

writefile("testfile", "foo foo")
commit()

addfile("testfile2", "blah blah")
commit()

writefile("testfile2", "foo foo")
commit()
R=base_revision()

check(mtn("ls", "duplicates","-r", R), 0, true, false)
check(qgrep("testfile", "stdout"))
check(qgrep("testfile2", "stdout"))
check(not qgrep("unique", "stdout"))
