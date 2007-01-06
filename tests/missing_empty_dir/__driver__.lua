
mtn_setup()

mkdir("foo")
mkdir("bar")

check(mtn("add", "foo"), 0, false, true)
check(mtn("add", "bar"), 0, false, true)
commit()

remove("foo")

check(mtn("status"), 1, false, false)

writefile("foo", "foo")

check(mtn("status"), 1, false, false)

