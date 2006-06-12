
mtn_setup()
revs = {}

addfile("testfile", "blah blah")
commit()
revs.t = base_revision()

writefile("testfile", "other other")
commit("otherbranch")
revs.o = base_revision()

writefile("testfile", "third third")
commit("somebranch")
revs.s = base_revision()
check(mtn("cert", revs.s, "branch", "otherbranch"), 0, false, false)

writefile("testfile", "double double")
commit("nobranch")
revs.n = base_revision()
check(mtn("db", "kill_branch_certs_locally", "nobranch"))

check(mtn("checkout", "--branch=testbranch", "--revision", revs.t, "codir"), 0, false, false)
check(grep('^ *branch "testbranch"', "codir/_MTN/options"), 0, false, false)
-- make sure that updating to a rev in one other branch puts us in that branch
check(indir("codir", mtn("update", "--revision", revs.o)), 0, false, false)
check(grep('^ *branch "otherbranch"', "codir/_MTN/options"), 0, false, false)

-- updating to a rev in multiple branches, including current branch, leaves branch alone
check(indir("codir", mtn("update", "-r", revs.s)), 0, false, false)
check(grep('^ *branch "otherbranch"', "codir/_MTN/options"), 0, false, false)

-- but updating to a rev in multiple branches that _don't_ include the current one, fails
-- first go back out to TR
check(indir("codir", mtn("update", "-r", revs.t)), 0, false, false)
check(grep('^ *branch "testbranch"', "codir/_MTN/options"), 0, false, false)
-- and now jumping to SR directly should fail
check(indir("codir", mtn("update", "-r", revs.s)), 1, false, false)
check(grep('^ *branch "testbranch"', "codir/_MTN/options"), 0, false, false)

-- updating to a rev in no branches at all succeeds, and leaves current branch alone
check(indir("codir", mtn("update", "-r", revs.n)), 0, false, false)
check(grep('^ *branch "testbranch"', "codir/_MTN/options"), 0, false, false)
