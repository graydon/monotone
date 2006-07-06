
mtn_setup()

-- Create a single revision in branch1
-- 
--          root (branch1)
--          
-- branch1 heads: root
revs = {}

-- like the normal commit function, except it catches the output
function local_ci(br)
  check(mtn("commit", "--message=blah-blan", "--branch", br), 0, true, true)
end

writefile("f", "base data")

check(mtn("add", "f"), 0, false, false)
local_ci("branch1")
check(not qgrep('creates divergence', "stderr"))
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
local_ci("branch1")
check(not qgrep('creates divergence', "stderr"))
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
local_ci("branch1")
revs.child2 = base_revision()
check(qgrep('this revision creates divergence', "stderr"))

check(mtn("--branch=branch1", "update"), 0, false, true)
check(qgrep('has multiple heads', "stderr"))
        
-- Create a new branch
-- 
--          root (branch1)
--          /           \
--    child1 (branch1)   child2 (branch1)
--        /
--     new1 (branch2)
--
-- branch1 heads: child1, child2
-- branch2 heads: new2

revert_to(revs.child1)

writefile("f", "new1 data")
local_ci("branch2")
revs.new1 = base_revision()
check(not qgrep('this revision creates divergence', "stderr"))

revert_to(revs.child2)

writefile("f", "new2 data")
local_ci("branch2")
revs.new2 = base_revision()
check(qgrep('this revision creates divergence', "stderr"))
