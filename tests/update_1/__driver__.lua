
mtn_setup()

-- We have A1 -> B1, A and B different branches.  Update from A1
-- shouldn't take us to B1.  Then propagate B -> A, and update from A1;
-- now this should take us to B1.

addfile("testfile", "stuff stuff")
commit("a")
a1 = base_revision()

writefile("testfile", "nonsense nonsense")
commit("b")
b1 = base_revision()

revert_to(a1)
-- Put in an explicit --branch, because REVERT_TO is not smart about
-- such things.
check(mtn("--branch=a", "update"), 0, false, false)
check(a1 == base_revision())

check(mtn("propagate", "b", "a"), 0, false, false)

revert_to(a1)
-- Put in an explicit --branch, because REVERT_TO is not smart about
-- such things.
check(mtn("--branch=a", "update"), 0, false, false)
check(b1 == base_revision())
