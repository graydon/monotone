
mtn_setup()

-- This tests the case where we have something like A1 -> A2 -> B1 ->
-- B2, where A and B are different branches, and the user propagates A
-- to B.  This should be a no-op; no merge should be performed.

-- The same applies when the heads of A and B are actually equal.

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

copy("test.db", "test2.db")
check(mtn("propagate", "a", "b"), 0, false, false)

check_same_db_contents("test.db", "test2.db")

check(mtn("cert", heads.b, "branch", "c"), 0, false, false)
copy("test.db", "test3.db")
check(mtn("propagate", "b", "c"), 0, false, false)

check_same_db_contents("test.db", "test3.db")
