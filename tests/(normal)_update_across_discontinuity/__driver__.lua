
mtn_setup()

-- testing that update is able to jump over discontinuities in
-- branches.  If we have A1 -> B1 -> A2, then updating branch A from A1
-- should get to A2.

addfile("testfile", "main branch data")
commit("mainbranch")
a1 = base_revision()

writefile("testfile", "first branch data 1")
commit("firstbranch")
b1 = base_revision()

writefile("testfile", "first branch data 2")
commit("mainbranch")
a2 = base_revision()


revert_to(a1)

check(mtn("update", "--branch=mainbranch"), 0, false, false)
got = base_revision()

check(got ~= a1)
check(got == a2)
