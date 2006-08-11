
mtn_setup()

mkdir("dir")
addfile("dir/file", "file")
commit()

remove("dir")
writefile("dir", "this isn't a directory")

check(mtn("status"), 1, false, false)
check(mtn("diff"), 1, false, false)
