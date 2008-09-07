
mtn_setup()

addfile("testfile", "blah blah")
commit()

writefile("testfile", "foo foo")
commit()

addfile("testfile2", "blah blah")
commit()
R=base_revision()

check(mtn("ls", "duplicates","-r", R), 0, true, false)
check(not qgrep("testfile", "stdout"))
