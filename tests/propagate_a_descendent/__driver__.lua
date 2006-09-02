
mtn_setup()

-- This tests the case where we have something like A1 -> A2 -> B1 ->
-- B2, where A and B are different branches, and the user propagates B
-- to A.  The result should be B2 being certed into branch A, with no
-- merge performed.

heads = {}

addfile("testfile", "foo")
commit("a")

writefile("testfile", "bar")
commit("a")
heads.a = base_revision()

writefile("testfile", "baz")
commit("b")

writefile("testfile", "quux")
commit("b")
heads.b = base_revision()

check(mtn("propagate", "b", "a"), 0, false, false)
check(mtn("heads", "--branch=b"), 0, true, false)
check(qgrep(heads.b, "stdout"))
check(mtn("heads", "--branch=a"), 0, true, false)
check(qgrep(heads.b, "stdout"))
