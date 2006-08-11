
mtn_setup()

-- A
-- |\
-- B C
--  \|
--   D
-- ^testbranch
--   ^otherbranch
revs = {}

check(mtn("automate", "leaves"), 0, 0, false)

addfile("testfile", "blah blah")
commit()
revs.a = base_revision()

check(mtn("automate", "leaves"), 0, true, false)
check(trim(readfile("stdout")) == revs.a)

writefile("testfile", "other stuff")
commit()
revs.b = base_revision()

check(mtn("automate", "leaves"), 0, true, false)
check(trim(readfile("stdout")) == revs.b)

revert_to(revs.a)

addfile("otherfile", "other blah blah")
commit("otherbranch")
revs.c = base_revision()

check(mtn("automate", "leaves"), 0, true, false)
canonicalize("stdout")
tmp = {revs.b, revs.c}
table.sort(tmp)
writefile("tmp", table.concat(tmp, "\n").."\n")
check(samefile("stdout", "tmp"))

check(mtn("propagate", "testbranch", "otherbranch"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.d = base_revision()

check(revs.d ~= revs.b)
check(revs.d ~= revs.c)

check_same_stdout(mtn("automate", "leaves"), mtn("automate", "heads", "otherbranch"))
check_different_stdout(mtn("automate", "leaves"), mtn("automate", "heads", "testbranch"))
