
mtn_setup()

-- This tests a case where file normalisation previously wasn't
-- working correctly, leading to:

-- --- correct     Thu Apr 28 15:38:27 2005
-- +++ testfile    Thu Apr 28 15:38:36 2005
-- @@ -5,6 +5,10 @@
--  3
--  4
--  a
-- +2
-- +3
-- +4
-- +a
--  q
--  d
--  g

-- merge(1) can handle this merge correctly.

check(get("parent"))
check(get("left"))
check(get("right"))
check(get("correct"))

copy("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
parent = base_revision()

copy("left", "testfile")
commit()

revert_to(parent)

copy("right", "testfile")
commit()

check(mtn("--branch=testbranch", "merge"), 0, false, false)

check(mtn("update"), 0, false, false)
check(samefile("testfile", "correct"))
