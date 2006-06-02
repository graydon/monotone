
mtn_setup()

writefile("foo", "the foo file")

writefile("bar", "the bar file")

writefile("missingfoo", "foo\n")

writefile("missingbar", "bar\n")

check(cmd(mtn("ls", "missing")), 0, false)

check(cmd(mtn("add", "foo", "bar")), 0, false, false)
check(cmd(mtn("ls", "missing")), 0, false)

remove("foo")
check(cmd(mtn("ls", "missing")), 0, true, 0)
canonicalize("stdout")
check(samefile("stdout", "missingfoo")) 

writefile("foo")
check(cmd(mtn("drop", "foo")), 0, false, false)
remove("foo")
check(cmd(mtn("ls", "missing")), 0, 0, 0)

commit()
check(cmd(mtn("ls", "missing")), 0, 0, 0)

remove("bar")
check(cmd(mtn("ls", "missing")), 0, true, 0)
canonicalize("stdout")
check(samefile("stdout", "missingbar")) 

writefile("bar")
check(cmd(mtn("drop", "bar")), 0, false, false)
remove("bar")
check(cmd(mtn("ls", "missing")), 0, 0, 0)
