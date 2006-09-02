
mtn_setup()

-- setup saves given branch
check(mtn("setup", "setupdir", "--branch=testbranch"), 0, false, false)
writefile("setupdir/foo", "blah blah")
check(indir("setupdir", mtn("add", "foo")), 0, false, false)
check(indir("setupdir", mtn("commit", "--message=foo")), 0, false, false)

-- checkout saves given branch
check(mtn("checkout", "--branch=testbranch", "codir"), 0, false, false)
check(samefile("setupdir/foo", "codir/foo"))
writefile("codir/foo", "other other")
check(indir("codir", mtn("commit", "--message=foo")), 0, false, false)

-- log doesn't affect given branch
check(indir("codir", mtn("log")), 0, false, false)
writefile("codir/foo", "more more")
check(indir("codir", mtn("commit", "--message=foo")), 0, false, false)
check(mtn("ls", "branches"), 0, true, false)
check(trim(readfile("stdout")) == "testbranch")

-- commit saves given branch
writefile("codir/foo", "newbranch newbranch")
check(indir("codir", mtn("commit", "--branch=otherbranch", "--message=foo")), 0, false, false)
writefile("codir/foo", "newbranch 2 newbranch 2")
check(indir("codir", mtn("commit", "--message=foo")), 0, false, false)
check(mtn("checkout", "--branch=otherbranch", "otherdir"), 0, false, false)
check(samefile("codir/foo", "otherdir/foo"))

-- update saves the given branch
check(mtn("checkout", "--branch=testbranch", "updir"), 0, false, false)
check(indir("updir", mtn("update", "--branch=otherbranch")), 0, false, false)
check(samefile("otherdir/foo", "updir/foo"))
writefile("otherdir/foo", "yet another chunk of entropy")
check(indir("otherdir", mtn("commit", "--message=foo")), 0, false, false)
check(indir("updir", mtn("update")), 0, false, false)
check(samefile("otherdir/foo", "updir/foo"))

-- merge doesn't affect given branch
check(mtn("co", "--branch=testbranch", "third1"), 0, false, false)
writefile("third1/a", "1a")
check(indir("third1", mtn("add", "a")), 0, false, false)
check(indir("third1", mtn("commit", "--branch=third", "--message=foo")), 0, false, false)
check(mtn("co", "--branch=testbranch", "third2"), 0, false, false)
writefile("third2/b", "2b")
check(indir("third2", mtn("add", "b")), 0, false, false)
check(indir("third2", mtn("commit", "--branch=third", "--message=foo")), 0, false, false)
check(indir("codir", mtn("merge", "--branch=third")), 0, false, false)
check(mtn("automate", "heads", "third"), 0, true, false)
rename("stdout", "old-third-heads")
writefile("codir/foo", "more more")
check(indir("codir", mtn("commit", "--message=foo")), 0, false, false)
-- we check that this didn't create a new head of branch third
check(mtn("automate", "heads", "third"), 0, true, false)
check(samefile("stdout", "old-third-heads"))
