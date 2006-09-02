-- -*-lua-*-
mtn_setup()

-- automate branches on empty db should return successful and empty
check(mtn("automate", "branches"), 0, true, true)
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

-- automate branches should list 2 branches now
check(mtn("automate", "branches"), 0, true, true)
check(samelines("stdout", {"otherbranch", "testbranch"}))

-- Create an ignore_branch hook to pass in
check(get("ignore_branch.lua"))

-- if we make a change in the branch.to.be.ignored it should not turn up in the list
copy("foo.testbranch", "in_ignored")
check(mtn("--rcfile=ignore_branch.lua", "add", "in_ignored"), 0, false, false)
commit("branch.to.be.ignored")
check(mtn("--rcfile=ignore_branch.lua", "automate", "branches"),0,true,true)
check(samelines("stdout", {"otherbranch", "testbranch"}))
