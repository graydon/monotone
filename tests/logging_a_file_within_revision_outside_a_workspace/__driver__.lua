
mtn_setup()

writefile("foo1", "foo file 1")
writefile("foo2", "foo file 2")

check(mtn("add", "foo1"), 0, false, false)
commit()
rev = base_revision()

remove("_MTN")
check(mtn("log", "--revision", rev, "foo1"), 0, false, false)
