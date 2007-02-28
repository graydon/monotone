
mtn_setup()

-- x_branch   y_branch
--  
--   A
--   |\   this edge says rename(x, y)
--   | ----------
--   |           \
--   B            E--------\
--   |            |         |
--   |            F         |
--   C            |         |
--   |\propagate1 |         |
--   | -----------G         |
--   |            |         J
--   |            H         |
--   D            |         |
--    \propagate2 |         |
--     -----------I---------K

writefile("x", "data of state A")
writefile("foo", "extra blah blah foo")
writefile("bar", "extra blah blah bar")
writefile("baz", "extra blah blah baz")
writefile("quux", "extra blah blah quux")

revs = {}

-- produce state A
check(mtn("add", "x"), 0, false, false)
commit("branch.x")
revs.a = base_revision()

-- produce state B
writefile("x", "data of state B")
commit("branch.x")

-- produce state C
writefile("x", "data of state C")
commit("branch.x")
revs.c = base_revision()

-- produce state E
revert_to(revs.a)
check(mtn("rename", "--bookkeep-only", "x", "y"), 0, false, false)
rename("x", "y")
commit("branch.y")
revs.e = base_revision()

-- produce state F
check(mtn("add", "foo"), 0, false, false)
commit("branch.y")

-- produce state G
check(mtn("propagate", "branch.x", "branch.y"), 0, false, false)
check(mtn("--branch=branch.y", "update"), 0, false, false)
revs.g = base_revision()
check(qgrep('state C', "y"))

-- produce state D
revert_to(revs.c)
writefile("x", "data of state D")
check(mtn("add", "bar"), 0, false, false)
commit("branch.x")

-- produce state H
revert_to(revs.g)
remove("bar")
check(mtn("add", "baz"), 0, false, false)
commit("branch.y")

-- produce state I
check(mtn("propagate", "branch.x", "branch.y"), 0, false, false)
check(mtn("--branch=branch.y", "update"), 0, false, false)
check(qgrep('state D', "y"))

-- produce state J
revert_to(revs.e)
remove("foo")
remove("bar")
remove("baz")

check(mtn("add", "quux"), 0, false, false)
commit("branch.y")

-- produce state K
check(mtn("--branch=branch.y", "merge"), 0, false, false)
check(mtn("--branch=branch.y", "update"), 0, false, false)

check(mtn("automate", "get_manifest_of"), 0, true)
rename("stdout", "manifest")
check(qgrep('"y"', "manifest"))
check(not qgrep('"x"', "manifest"))
check(exists("y"))
check(qgrep('state D', "y"))
