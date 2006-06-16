
mtn_setup()

writefile("foo1", "foo file 1")

check(mtn("add", "foo1"), 0, false, false)
commit()
parent = base_revision()

-- should return 'no changes' (i.e. same as diff without --revision=<current>)
check_same_stdout(mtn("diff"), mtn("diff", "--revision", parent))

writefile("foo1", "foo changed file")

-- should show local changes against wc's base rev
check_same_stdout(mtn("diff"), mtn("diff", "--revision", parent))
