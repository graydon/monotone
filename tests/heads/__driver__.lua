
mtn_setup()

-- Create a single revision in branch1
-- 
--          root (branch1)
--          
-- branch1 heads: root

revs = {}

addfile("f", "base data")
commit("branch1")
revs.root = base_revision()

check(mtn("--branch=branch1", "heads"), 0, true, false)
check(qgrep(revs.root, "stdout"))

-- Create a child
-- 
--          root (branch1)
--          /
--    child1 (branch1)
--
-- branch1 heads: child1

writefile("f", "child1 data")
commit("branch1")
revs.child1 = base_revision()

check(mtn("--branch=branch1", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(qgrep(revs.child1, "stdout"))

-- Create another child
-- 
--          root (branch1)
--          /           \
--    child1 (branch1)   child2 (branch1)
--
-- branch1 heads: child1, child2

revert_to(revs.root)
writefile("f", "child2 data")
commit("branch1")
revs.child2 = base_revision()

check(mtn("--branch=branch1", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(qgrep(revs.child1, "stdout"))
check(qgrep(revs.child2, "stdout"))

-- Branch from the second child into branch2
-- 
--          root (branch1)
--          /           \
--    child1 (branch1)   child2 (branch1)
--                         \
--                          child3 (branch2)
--
-- branch1 heads: child1, child2
-- branch2 heads: child3

writefile("f", "child3 data")
commit("branch2")
revs.child3 = base_revision()

check(mtn("--branch=branch1", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(qgrep(revs.child1, "stdout"))
check(qgrep(revs.child2, "stdout"))
check(not qgrep(revs.child3, "stdout"))
check(mtn("--branch=branch2", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(not qgrep(revs.child1, "stdout"))
check(not qgrep(revs.child2, "stdout"))
check(qgrep(revs.child3, "stdout"))

-- Branch from the first child into branch2
-- 
--          root (branch1)
--          /           \
--    child1 (branch1)   child2 (branch1)
--       /                 \
--   child4 (branch2)       child3 (branch2)
--
-- branch1 heads: child1, child2
-- branch2 heads: child3, child4

revert_to(revs.child1)
writefile("f", "child4 data")
commit("branch2")
revs.child4 = base_revision()

check(mtn("--branch=branch1", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(qgrep(revs.child1, "stdout"))
check(qgrep(revs.child2, "stdout"))
check(not qgrep(revs.child3, "stdout"))
check(not qgrep(revs.child4, "stdout"))
check(mtn("--branch=branch2", "heads"), 0, true, false)
check(not qgrep(revs.root, "stdout"))
check(not qgrep(revs.child1, "stdout"))
check(not qgrep(revs.child2, "stdout"))
check(qgrep(revs.child3, "stdout"))
check(qgrep(revs.child4, "stdout"))
