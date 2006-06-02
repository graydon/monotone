
mtn_setup()

-- This is (was) a real merge error.  'right' contains only a single
-- added function; when it was really merged with 'left', the last
-- line of this function was lost.

-- This may actually be (have been) a bug in the unidiff algorithm;
-- 'diff' and 'mtn diff' produce(d) different results when calculating
-- diff(parent, left).

getfile("parent")
getfile("left")
getfile("right")
getfile("correct")

copyfile("parent", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit(testbranch)
parent = base_revision()

copyfile("left", "testfile")
commit()

revert_to(parent)

copyfile("right", "testfile")
commit()

check(cmd(mtn("merge")), 0, false, false)

check(cmd(mtn("update")), 0, false, false)
check(samefile("testfile", "correct"))
