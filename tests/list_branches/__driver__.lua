
mtn_setup()

-- ls branches on empty db should return successful and empty
check(mtn("ls", "branches"), 0, true, true)
check(fsize("stdout") == 0)
check(fsize("stderr") == 0)

-- Let's create some branches, so we have stuff to list
writefile("foo.testbranch", "this is the testbranch version")
writefile("foo.otherbranch", "this version goes in otherbranch")

copy("foo.testbranch", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()

copy("foo.otherbranch", "foo")
commit("otherbranch")

-- ls branches should list 2 branches now
check(mtn("ls", "branches"), 0, true, true)
check(samelines("stdout", {"otherbranch", "testbranch"}))

check(mtn("ls", "branches", "otherbr*"),0,true,true)
check(samelines("stdout", {"otherbranch"}))

check(mtn("ls", "branches", "--exclude", "testbr*"), 0, true, true)
check(samelines("stdout", {"otherbranch"}))

-- Create an ignore_branch hook to pass in
check(get("ignore_branch.lua"))

-- if we make a change in the branch.to.be.ignored it should not turn up in the list
copy("foo.testbranch", "in_ignored")
check(mtn("--rcfile=ignore_branch.lua", "add", "in_ignored"), 0, false, false)
commit("branch.to.be.ignored")
check(mtn("--rcfile=ignore_branch.lua", "ls", "branches"),0,true,true)
check(samelines("stdout", {"otherbranch", "testbranch"}))
