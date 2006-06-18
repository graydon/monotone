
mtn_setup()

addfile("testfile", "blah blah")
commit()

check(not exists("_MTN/inodeprints"))

check(mtn("refresh_inodeprints"))
check(exists("_MTN/inodeprints"))
check(fsize("_MTN/inodeprints") ~= 0)
