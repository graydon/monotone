mtn_setup()

addfile("foo", "blah blah")
commit()

remove("_MTN/format")
check(raw_mtn("status"), 1, false, false)

writefile("_MTN/format", "")
check(raw_mtn("status"), 1, false, false)

writefile("_MTN/format", "asdf")
check(raw_mtn("status"), 1, false, false)

writefile("_MTN/format", "1 2 3")
check(raw_mtn("status"), 1, false, false)
