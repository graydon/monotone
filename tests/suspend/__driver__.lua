include("common/selectors.lua")
mtn_setup()

addfile("testfile", "testbranch1")
commit()
REV1=base_revision()

writefile("testfile", "testbranch2")
commit()
REV2=base_revision()

-- check that suspend hides things from the h: selector

selmap("h:testbranch", {REV2})
check(mtn("suspend", REV2))
selmap("h:testbranch", {})

check(mtn("automate", "heads"), 0, true, false)
check(samelines("stdout", {}))

check(mtn("automate", "heads", "--ignore-suspend-certs"), 0, true, false)
check(samelines("stdout", {REV2}))

-- but still allows children to be committed

writefile("testfile", "testbranch3")
commit()
REV3=base_revision()

-- and those non-suspended children show up as normal

selmap("h:testbranch", {REV3})

-- introduce a second branch

writefile("testfile", "otherbranch1")
commit("otherbranch")
OREV1=base_revision()

-- check that both branches show up when we list all branches

check(mtn("ls", "branches"), 0, true, true)
check(samelines("stdout", {"otherbranch", "testbranch"}))

writefile("testfile", "otherbranch2")
commit("otherbranch")
OREV2=base_revision()

-- now suspend the second branch and check it is gone when we list branches

check(mtn("suspend", OREV2))
check(mtn("ls", "branches"), 0, true, true)
check(samelines("stdout", {"testbranch"}))

-- Make a second, but not suspended head

check(mtn("up", "-r", OREV1), 0, false, false)

writefile("testfile", "otherbranch2b")
commit("otherbranch")
OREV2b=base_revision()

-- Check that only the non-suspended head shows up

selmap("h:otherbranch", {OREV2b})

-- Check that a revision suspended in one branch can still appear in another

check(mtn("approve", OREV2, "-b", "testbranch"))
selmap("h:testbranch", {OREV2})

-- Check that update ignores the suspended revision when there is a non-suspended revision

check(mtn("up", "-r", OREV1, "-b", "otherbranch"), 0, false, false)
check(mtn("up"), 0, false, false)
check(base_revision() == OREV2b)

-- Check that update complains in that case if we're ignoring suspend certs

check(mtn("up", "-r", OREV1, "-b", "otherbranch"), 0, false, false)
check(mtn("up", "--ignore-suspend-certs"), 1, false, false)
check(base_revision() == OREV1)

-- Check that update complains about multiple heads when all candidates are suspended

check(mtn("suspend", OREV2b))
check(mtn("up", "-r", OREV1, "-b", "otherbranch"), 0, false, false)
check(mtn("up"), 1, false, false)
check(base_revision() == OREV1)

-- check that the --ignore-suspend option works for listing branches

check(mtn("ls", "branches"), 0, true, true)
check(samelines("stdout", {"testbranch"}))

check(mtn("ls", "branches", "--ignore-suspend-certs"), 0, true, true)
check(samelines("stdout", {"otherbranch", "testbranch"}))

