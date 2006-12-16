
include("common/selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit()
R1=base_revision()

writefile("testfile", "stuff stuff")
commit("otherbranch")
R2=base_revision()

writefile("testfile", "thing thing")
commit("branch")
R3=base_revision()

check(mtn("tag", R1, "foo"), 0, false, false)
check(mtn("tag", R2, "bar"), 0, false, false)
check(mtn("tag", R3, "foobarbaz"), 0, false, false)

writefile("testfile", "blub blub")
check(mtn("commit", "-m", "foo", "-b", "bar", "--author", "joe@user.com"), 0, false, false)
R4=base_revision()

selmap("b:testbranch", {R1})
selmap("b:otherbranch", {R2})
selmap("b:branch", {R3})
selmap("b:test*", {R1})
selmap("b:*branch*", {R1, R2, R3})

selmap("t:foo", {R1})
selmap("t:bar", {R2})
selmap("t:foobarbaz", {R3})
selmap("t:*bar", {R2})
selmap("t:*bar*", {R2, R3})

selmap("a:joe*", {R4})
selmap("a:*user.com", {R4})

