
mtn_setup()

addfile("foo", "blah blah")
commit()

-- okay, now wd is on testbranch

-- setup a dir on otherbranch and make sure we stay on testbranch
check(mtn("setup", "setupdir", "--branch=otherbranch"), 0, false, false)
writefile("foo", "stuff stuff")
check(mtn("commit", "--message=foo"), 0, false, false)
check(mtn("automate", "heads", "testbranch"), 0, true, false)
rename("stdout", "headsout")
check(trim(readfile("headsout")) == base_revision())

-- now create a revision in otherbranch...
writefile("setupdir/blah", "yum yum")
check(indir("setupdir", mtn("add", "blah")), 0, false, false)
check(indir("setupdir", mtn("commit", "--message=foo")), 0, false, false)
-- and check it out
check(mtn("checkout", "codir", "--branch=otherbranch"), 0, false, false)
-- and make sure we still stayed on testbranch
writefile("foo", "more more")
check(mtn("commit", "--message=foo"), 0, false, false)
check(mtn("automate", "heads", "testbranch"), 0, true, false)
rename("stdout", "headsout")
check(trim(readfile("headsout")) == base_revision())
