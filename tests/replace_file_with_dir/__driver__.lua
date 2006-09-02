
mtn_setup()

addfile("file", "file")
commit()

remove("file")
mkdir("file")
check(mtn("status"), 1, false, false)
check(mtn("diff"), 1, false, false)
