mtn_setup()

mkdir("w1")

check(indir("w1", mtn("setup", ".", "-b", "testbranch")), 0, false, false)

writefile("w1/testfile", "ancestor\nancestor")
mkdir("w1/newroot")
writefile("w1/newroot/fileinroot",
	  "ordinals are bluejays, cardinals are cardinals")

check(indir("w1", mtn("add", "-R", ".")), 0, false, false)
check(indir("w1", mtn("commit", "--message", "blah-blah")), 0, false, false)
anc = indir("w1", {base_revision})[1]()

check(mtn("co", "-r", anc, "w2"), 0, false, false)
writefile("w2/testfile", "left\nancestor")
check(indir("w2", mtn("commit", "--message", "blah-blah")), 0, false, false)
left = indir("w2", {base_revision})[1]()

check(mtn("co", "-r", anc, "w3"), 0, false, false)
writefile("w3/testfile", "ancestor\nright")
check(indir("w3", mtn("commit", "--message", "blah-blah")), 0, false, false)
right = indir("w3", {base_revision})[1]()

check(indir("w3", mtn("merge_into_workspace", left)), 0, false, false)
check(qgrep("left", "w3/testfile"))
check(qgrep("right", "w3/testfile"))
check(not qgrep("ancestor", "w3/testfile"))

check(indir("w3", mtn("pivot_root", "newroot", "oldroot")),
      0, nil, false)

check(exists("w3/fileinroot"))
check(not exists("w3/testfile"))
check(exists("w3/oldroot/testfile"))
check(not exists("w3/oldroot/fileinroot"))

check(indir("w3", mtn("commit", "--message", "blah-blah")), 0, false, false)
