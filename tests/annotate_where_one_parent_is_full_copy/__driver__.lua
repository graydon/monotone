-- -*-lua-*-

mtn_setup()

--
--     A      Where B -> D is no change, but 
--    / \     C -> D shows a delta for a line modified in
--   B   C    B.
--    \ /
--     D
--
revs = {}

writefile("A", "a\nb\nc\n")
writefile("B", "a\nb\nx\ny\nc\n")

-- C == A
-- D == B

copy("A", "foo")
addfile("initialfile", "foo\nfile\n")
check(mtn("add", "foo"), 0, false, false)
commit()
revs.a = base_revision()

copy("B", "foo")
commit()
revs.b = base_revision()

revert_to(revs.a)
copy("A", "initialfile")
commit()
revs.c = base_revision()


check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.d = base_revision()

--
-- annotate foo should now be
-- REVA: a
-- REVA: b
-- REVB: x
-- REVB: y
-- REVA: c
--

check(mtn("annotate", "--revs-only", "foo"), 0, true, false)
check(greplines("stdout", {revs.a, revs.a, revs.b, revs.b, revs.a}))
