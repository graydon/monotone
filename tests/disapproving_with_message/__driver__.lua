
mtn_setup()

addfile("testfile", "foo")
commit()

writefile("testfile", "bar")
commit()
r_sha = base_revision()

check(mtn("disapprove","-m","line1","-m","line2", r_sha), 0, false, false)
check(mtn("update"), 0, false, false)
check(mtn("log","--last=1"), 0, true, false)

check(qgrep("line1","stdout"))
check(qgrep("line2","stdout"))
