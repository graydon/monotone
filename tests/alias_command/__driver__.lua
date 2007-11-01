-- -*-lua-*-
-- 
-- We are testing annotate on this graph:
-- 
--       a
--      /|\
--     / | \
--    b* c* d*
--    |  |  | 
--    e* e* e*
--    \   \/
--     \   e
--      \ /
--       e
--       |
--       f*
-- 
-- The current annotate code will arbitrarily select one of the marked
-- e-revisions for lines coming from e.
-- 

mtn_setup()

check(get("extra_rc"))

addfile("foo", "first\nfoo\nthird\n")
commit()
rev_a = base_revision()

econtents = "first\nsecond\nthird\n"

writefile("foo", "first\nb\nthird\n")
commit()
writefile("foo", econtents)
commit()
rev_e1 = base_revision()

revert_to(rev_a)
writefile("foo", "first\nc\nthird\n")
commit()
writefile("foo", econtents)
commit()
rev_e2 = base_revision()

revert_to(rev_a)
writefile("foo", "first\nd\nthird\n")
commit()
writefile("foo", econtents)
commit()
rev_e3 = base_revision()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)

writefile("foo", econtents .. "fourth\n")
commit()
rev_f = base_revision()

check(mtn("praise", "foo", "--revs-only", "--rcfile=extra_rc"), 0, true, true)

check(qgrep(rev_a .. ": first", "stdout"))
check(qgrep(rev_e1 .. ": second", "stdout")
      or qgrep(rev_e2 .. ": second", "stdout")
      or qgrep(rev_e3 .. ": second", "stdout"))
check(qgrep(rev_a .. ": third", "stdout"))
check(qgrep(rev_f .. ": fourth", "stdout"))

check(mtn("blame", "foo", "--revs-only", "--rcfile=extra_rc"), 0, true, true)

check(qgrep(rev_a .. ": first", "stdout"))
check(qgrep(rev_e1 .. ": second", "stdout")
      or qgrep(rev_e2 .. ": second", "stdout")
      or qgrep(rev_e3 .. ": second", "stdout"))
check(qgrep(rev_a .. ": third", "stdout"))
check(qgrep(rev_f .. ": fourth", "stdout"))
