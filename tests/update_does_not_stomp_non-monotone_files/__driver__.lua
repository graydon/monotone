
mtn_setup()

-- This test is a bug report

-- 1. Alice checks out project, creates file foo
-- 2. Bob checks out project, creates foo, adds foo, and commits
-- 3. Now Alice does an update
--
-- monotone should warn her before stomping her non-revision controlled 'foo' file
--

writefile("initial", "some initial data")

writefile("foo.alice", "foo not revision controlled")
writefile("foo.bob", "foo checked into project")

-- Alice make project, writes foo, but doesn't check it in
mkdir("alicewd")
copy("initial", "alicewd/initial")
check(mtn("--branch=testbranch", "setup", "alicewd"), 0, false, false)
check(indir("alicewd", mtn("--root=.", "add", "initial")), 0, false, false)
check(indir("alicewd", mtn("--branch=testbranch", "--root=.", "commit", "-m", 'initial commit')), 0, false, false)
copy("foo.alice", "alicewd/foo")

-- Bob does add of file foo, and commits
check(mtn("--branch=testbranch", "checkout", "bobwd"), 0, false, false)
copy("foo.bob", "bobwd/foo")
check(indir("bobwd", mtn("--root=.", "add", "foo")), 0, false, false)
check(indir("bobwd", mtn("--branch=testbranch", "--root=.", "commit", "-m", 'bob commit')), 0, false, false)
rev = indir("bobwd", {base_revision})[1]()

-- Alice does her update, discovers foo has been stomped!
check(indir("alicewd", mtn("--branch=testbranch", "--root=.", "update", "--revision", rev)), 1, false, false)
check(samefile("foo.alice", "alicewd/foo"))
