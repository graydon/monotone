
mtn_setup()

-- This is (was) a real merge error.  'right' contains only a single
-- added function; when it was really merged with 'left', the last
-- line of this function was lost.

-- This may actually be (have been) a bug in the unidiff algorithm;
-- 'diff' and 'mtn diff' produce(d) different results when calculating
-- diff(parent, left).

check(get("parent"))
check(get("left"))
check(get("right"))
check(get("correct"))

copy("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit(testbranch)
parent = base_revision()

copy("left", "testfile")
commit()

revert_to(parent)

copy("right", "testfile")
commit()

check(mtn("merge"), 0, false, false)

check(mtn("update"), 0, false, false)
check(samefile("testfile", "correct"))
