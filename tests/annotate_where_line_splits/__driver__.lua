-- -*-lua-*-

mtn_setup()
revs = {}

writefile("foo.A", "a\nident\nd\n")
writefile("foo.B", "a\nident\nb\n")
writefile("foo.C", "c\nident\nx\n")

-- foo.D, the ultimate version, as created by our merge3 hook:
-- a
-- ident
-- b
-- c
-- ident
-- d

check(get("merge.lua"))


copy("foo.A", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()
revs.a = base_revision()
L("revs.a = ", revs.a, "\n")

copy("foo.B", "foo")
commit()
revs.b = base_revision()
L("revs.b = ", revs.b, "\n")

revert_to(revs.a)

copy("foo.C", "foo")
commit()
revs.c = base_revision()
L("revs.c = ", revs.c, "\n")

check(mtn("--rcfile=./merge.lua", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.d = base_revision()
L("revs.d = ", revs.d, "\n")

--
-- annotate foo should now be
-- REVA: a
-- REVA: ident
-- REVB: b
-- REVC: c
-- REVA: ident
-- REVD: d
--

check(mtn("--debug", "annotate", "--revs-only", "foo"), 0, true, true)
check(greplines("stdout", {revs.a, revs.a, revs.b, revs.c, revs.a, revs.d}))
