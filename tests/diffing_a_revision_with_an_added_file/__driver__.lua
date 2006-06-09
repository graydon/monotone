
mtn_setup()

addfile("foo1", "foo file 1")
commit()
parent = base_revision()

addfile("foo2", "foo file 2")
commit()

check(mtn("diff", "--revision", parent, "--revision", base_revision()), 0, false, false)
