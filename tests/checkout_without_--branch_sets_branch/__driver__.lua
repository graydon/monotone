
mtn_setup()

addfile("foo", "blah blah")
commit()
rev = base_revision()

check(mtn("checkout", "--revision", rev, "codir1"), 0, false, false)
check(samefile("foo", "codir1/foo"))
writefile("codir1/foo", "hi maude")
-- verify that no branch is needed for commit
check(indir("codir1", mtn("commit", "--message=foo")), 0, false, false)

check(mtn("cert", rev, "branch", "otherbranch"))

-- but, now we can't checkout without a --branch...
-- need to make sure don't pick up branch from our local _MTN dir...
remove("_MTN")
check(mtn("checkout", "--revision", rev, "codir2"), 1, false, false)
check(mtn("checkout", "--revision", rev, "--branch=testbranch", "codir3"), 0, false, false)
check(samefile("foo", "codir3/foo"))
check(mtn("checkout", "--revision", rev, "--branch=otherbranch", "codir4"), 0, false, false)
check(samefile("foo", "codir4/foo"))
