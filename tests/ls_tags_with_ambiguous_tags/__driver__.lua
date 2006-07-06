
mtn_setup()

addfile("testfile", "blah blah")
commit()
R1=base_revision()

writefile("testfile", "foo foo")
commit()
R2=base_revision()

check(mtn("tag", R1, "ambig_tag"), 0, false, false)
check(mtn("tag", R2, "ambig_tag"), 0, false, false)

check(mtn("ls", "tags"), 0, true, false)
check(qgrep(R1, "stdout"))
check(qgrep(R2, "stdout"))
