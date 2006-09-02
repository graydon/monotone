
mtn_setup()

-- This tests the 'heads' command with a graph like:
--
--    r1 (branch1)
--    |
--    r2 (branch2)
--    |
--    r3 (branch1)
--
-- 'heads' on branch1 should show only r3, not r1.

revs = {}

-- Create R1
writefile("f", "r1 data")
check(mtn("add", "f"), 0, false, false)
commit("branch1")
revs[1] = base_revision()

-- Sanity check first...
check(mtn("--branch=branch1", "heads"), 0, true, false)
check(qgrep(revs[1], "stdout"))

-- Now create R2
writefile("f", "r2 data")
commit("branch2")
revs[2] = base_revision()

-- Now create R3
writefile("f", "r3 data")
commit("branch1")
revs[3] = base_revision()

-- Now check heads on branch1
check(mtn("--branch=branch1", "heads"), 0, true, false)
check(not qgrep(revs[1], "stdout"))
check(not qgrep(revs[2], "stdout"))
check(qgrep(revs[3], "stdout"))
