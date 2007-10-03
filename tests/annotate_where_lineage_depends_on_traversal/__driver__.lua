-- -*-lua-*-

mtn_setup()
revs = {}

writefile("foo.A", "a\nb\nc\nd\ne\n")
writefile("foo.B", "x\nb\np\nd\ne\n")
writefile("foo.C", "a\nb\nq\nd changed in C\ny\n")
-- foo.D, the ultimate version, as created by our merge3 hook:
-- a
-- b
-- p
-- d changed in C
-- e
-- added line

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
-- REVA: b
-- REVB: p
-- REVC: d changed in C
-- REVA: e
-- REVD: added line
--

check(mtn("--debug", "annotate", "--revs-only", "foo"), 0, true, true)
check(greplines("stdout", {revs.a, revs.a, revs.b, revs.c, revs.a, revs.d}))
