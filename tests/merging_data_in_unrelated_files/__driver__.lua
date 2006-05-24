
mtn_setup()

-- This test relies on file-suturing

addfile("foo", "irrelevant file\n")
commit()
anc = base_revision()

getfile("left", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()
left = base_revision()

revert_to(anc)

getfile("right", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()
right = base_revision()

xfail_if(true, cmd(mtn("--branch=testbranch", "merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)

writefile("expected_foo", "irrelevant file\n")

check(samefile("foo", "expected_foo"))
check(samefile("left", "testfile") or samefile("right", "testfile"))
