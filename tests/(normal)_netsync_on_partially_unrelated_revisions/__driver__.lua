
include("/common/netsync.lua")
mtn_setup()

-- This test relies on file-suturing

-- This tests netsync'ing
--
--   A   B
--    \ /
--     C
--
-- where A starts out shared, but B and C do not.

-- For analysis and discussion of solutions, see:
--   http://lists.gnu.org/archive/html/monotone-devel/2004-11/msg00043.html
-- There are other strategies that might be good besides the one
-- mentioned there; doing sideways deltas between heads, all sorts of
-- possibilities for maybe-efficient algorithms.

netsync.setup()

addfile("testfile1", "This is test file 1")
commit("testbranch")
base = base_revision()

netsync.pull("testbranch")

for _,i in pairs{{"automate", "graph"}, {"ls", "certs", base}} do
  check_same_stdout(mtn(unpack(i)), mtn2(unpack(i)))
end

remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."), 0, false, false)

addfile("testfile2", "This is test file 2")
commit("testbranch")
unrelated = base_revision()

xfail_if(true, mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)
merge = base_revision()

netsync.pull("testbranch")

check_same_stdout(mtn("automate", "graph"), mtn2("automate", "graph"))
for _,i in pairs{base, unrelated, merge} do
  check_same_stdout(mtn("ls", "certs", i),
                    mtn2("ls", "certs", i))
end
