
mtn_setup()

addfile("file", "file")

mkdir("dir")
check(mtn("add", "dir"), 0, false, false)

commit()

check(mtn("--execute", "mv", "file", "dir"), 0, false, false)
check(mtn("--execute", "mv", "dir", "foo"), 0, false, false)
check(mtn("revert", "dir"), 0, false, false)
