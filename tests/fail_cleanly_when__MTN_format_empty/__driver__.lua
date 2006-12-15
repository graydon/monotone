
mtn_setup()

addfile("foo", "blah blah")
commit()

remove("_MTN/format")
writefile("_MTN/format", "")
check(raw_mtn("status"), 1, false, false)
