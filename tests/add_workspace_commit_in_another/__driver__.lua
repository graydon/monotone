
mtn_setup()

-- This test relies on file-suturing

-- 1. Alice writes a file, does an add, *doesn't* do a commit, and sends patch
-- 2. Bob applies (modified) patch to tree, does the add, then a commit.
-- 3. Now Alice does an update (resolves the merge conflict, choosing Bob's changes).

writefile("initial", "some initial data")

check(get("foo.alice"))

check(get("foo.bob"))

-- Alice does her add
mkdir("alicewd")
copy("initial", "alicewd/initial")
check(mtn("--branch=testbranch", "setup", "alicewd"), 0, false, false)
check(indir("alicewd", mtn("--root=.", "add", "initial")), 0, false, false)
check(indir("alicewd", mtn("--root=.", "commit", "-m", 'initial commit')), 0, false, false)
copy("foo.alice", "alicewd/foo")
check(indir("alicewd", mtn("add", "--root=.", "foo")), 0, false, false)
-- Note, alice does not commit this add...

-- Bob does add of same file, with edits, and commits
check(mtn("--branch=testbranch", "checkout", "bobwd"), 0, false, false)
copy("foo.bob", "bobwd/foo")
check(indir("bobwd", mtn("--root=.", "add", "foo")), 0, false, false)
check(indir("bobwd", mtn("--root=.", "commit", "-m", 'bob commit')), 0, false, false)
rev = indir("bobwd", {base_revision})[1]()

-- Alice does her update, then attempts, eg., a diff
xfail_if(true, indir("alicewd", mtn("--root=.", "update", "--revision", rev)), 0, false, false)
check(indir("alicewd", mtn("--root=.", "diff")), 0, false, false)
