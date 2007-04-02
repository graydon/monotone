
mtn_setup()

addfile("testfile", "floooooo")
check(mtn("commit", "--author=the_author", "--date=1999-12-31T12:00:00", "--branch=foo", "--message=foo"), 0, false, false)
rev = base_revision()
check(mtn("log", "--from", rev), 0, true, false)

check(qgrep('^[\\| ]+Author: the_author', "stdout"))
check(qgrep('^[\\| ]+Date: 1999-12-31T12:00:00', "stdout"))

writefile("testfile", "oovel")
check(mtn("commit", "--date=1999-12-31T12:00foo", "--branch=foo", "--message=foo"), 1, false, false)
