
mtn_setup()

writefile("foo", "the foo file")

writefile("bar", "the bar file")

writefile("missingfoo", "foo\n")

writefile("missingbar", "bar\n")

check(mtn("ls", "missing"), 0, false)

check(mtn("add", "foo", "bar"), 0, false, false)
check(mtn("ls", "missing"), 0, false)

remove("foo")
check(mtn("ls", "missing"), 0, true, 0)
canonicalize("stdout")
check(samefile("stdout", "missingfoo")) 

writefile("foo")
check(mtn("drop", "--bookkeep-only", "foo"), 0, false, false)
remove("foo")
check(mtn("ls", "missing"), 0, 0, 0)

commit()
check(mtn("ls", "missing"), 0, 0, 0)

remove("bar")
check(mtn("ls", "missing"), 0, true, 0)
canonicalize("stdout")
check(samefile("stdout", "missingbar")) 

writefile("bar")
check(mtn("drop", "--bookkeep-only", "bar"), 0, false, false)
remove("bar")
check(mtn("ls", "missing"), 0, 0, 0)
