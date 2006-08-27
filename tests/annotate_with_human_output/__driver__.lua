mtn_setup()

revs = {}

addfile("foo", "a\nb\nc\nd\n")
commit()
rev1 = base_revision()

writefile("foo", "a\nB\nc\nD\n")
commit()
rev2 = base_revision()

rev1_line = string.format("%s.. by tester", rev1:sub(0, 8))
rev2_line = string.format("%s.. by tester", rev2:sub(0, 8))

check(mtn("annotate", "foo"), 0, true, false)
check(greplines("stdout", {rev1_line, rev2_line, rev1_line, rev2_line}))
