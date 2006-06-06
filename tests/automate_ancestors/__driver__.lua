
mtn_setup()

check(mtn("automate", "ancestors", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
revs = {}

addfile("testfile", "A")
commit()
revs.a = base_revision()

writefile("testfile", "B")
commit()
revs.b = base_revision()

revert_to(revs.a)

writefile("testfile", "C")
commit()
revs.c = base_revision()

writefile("testfile", "D")
commit()
revs.d = base_revision()

revert_to(revs.c)

addfile("otherfile", "E")
commit()
revs.e = base_revision()

check(mtn("explicit_merge", revs.d, revs.e, "testbranch"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.f = base_revision()

check(revs.f ~= revs.d)
check(revs.f ~= revs.e)

-- Now do some checks

check(mtn("automate", "ancestors", revs.a), 0, 0, false)

check(mtn("automate", "ancestors", revs.b), 0, true, false)
canonicalize("stdout")
writefile("tmp", revs.a.."\n")
check(samefile("tmp", "stdout"))

check(mtn("automate", "ancestors", revs.e), 0, true, false)
canonicalize("stdout")
tmp = {revs.a, revs.c}
table.sort(tmp)
writefile("tmp", table.concat(tmp, "\n").."\n")
check(samefile("tmp", "stdout"))

check(mtn("automate", "ancestors", revs.f), 0, true, false)
canonicalize("stdout")
tmp = {revs.d, revs.a, revs.c, revs.e}
table.sort(tmp)
writefile("tmp", table.concat(tmp, "\n").."\n")
check(samefile("tmp", "stdout"))

check(mtn("automate", "ancestors", revs.d, revs.f), 0, true, false)
canonicalize("stdout")
tmp = {revs.d, revs.a, revs.c, revs.e}
table.sort(tmp)
writefile("tmp", table.concat(tmp, "\n").."\n")
check(samefile("tmp", "stdout"))

check(mtn("automate", "ancestors", revs.a, revs.b, revs.c), 0, true, false)
canonicalize("stdout")
writefile("tmp", revs.a.."\n")
check(samefile("tmp", "stdout"))
