-- Test of annotation of merged but not committed files.
-- Does not currently work, see comments in cmd_files.cc.

mtn_setup()

check(get("testfile"))
check(get("left"))
check(get("right"))
check(get("merged"))
check(get("expected-annotation"))

addfile("testfile")
commit()
anc = base_revision()

revert_to(anc)
copy("left", "testfile")
commit()
left = base_revision()

revert_to(anc)
copy("right", "testfile")
commit()
right = base_revision()

check(mtn("merge_into_workspace", left), 0, false, false)

-- testfile should be the same as merged
check(samefile("merged", "testfile"))

-- annotate should do something sensible
xfail(mtn("annotate", "--brief", "testfile"), 0, {"expected-annotation"}, nil)

-- a commit at this point should succeed
commit()
