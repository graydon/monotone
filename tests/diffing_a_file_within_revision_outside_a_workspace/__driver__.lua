
mtn_setup()

writefile("foo1", "foo file 1")
writefile("foo2", "foo file 2")

check(mtn("add", "foo1"), 0, false, false)
commit()
parent = base_revision()

check(mtn("add", "foo2"), 0, false, false)
commit()
second = base_revision()

remove("_MTN")
check(mtn("diff", "--revision", parent, "--revision", second), 0, false, false)
-- check it works when specifying files
check(mtn("diff", "--revision", parent, "--revision", second, "foo2"), 0, false, false)
