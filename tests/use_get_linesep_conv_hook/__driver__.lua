
mtn_setup()
revs = {}

-- This test excercises the common case of wanting to do newline 
-- character conversion so that win32 users can have native line endings
-- in their workspace.

writefile("foo.crlf", "foo\r\n")
writefile("foo.lf", "foo\n")

writefile("foofoo.crlf", "foo\r\nfoo\r\n")
writefile("foofoo.lf", "foo\nfoo\n")

check(get("linesep.lua"))

copy("foo.crlf", "foo")
check(mtn("--rcfile=linesep.lua", "add", "foo"), 0, false, false)
check(mtn("--rcfile=linesep.lua", "--branch=foo", "commit", "-m", "foo"), 0, false, false)
revs.foo = base_revision()

copy("foofoo.crlf", "foo")
check(mtn("--rcfile=linesep.lua", "commit", "-m", "foofoo"), 0, false, false)
revs.foofoo = base_revision()

remove("_MTN")
check(mtn("--rcfile=linesep.lua", "co", "--revision", revs.foo, "."), 0, false, false)
check(samefile("foo", "foo.crlf"))

check(mtn("--rcfile=linesep.lua", "checkout", "--revision", revs.foo, "foo_crlf"), 0, false, false)
check(samefile("foo.crlf", "foo_crlf/foo"))

check(mtn("--rcfile=linesep.lua", "checkout", "--revision", revs.foofoo, "foo_foo_crlf"), 0, false, false)
check(samefile("foofoo.crlf", "foo_foo_crlf/foo"))

-- no rcfile here
check(mtn("checkout", "--revision", revs.foo, "foo_lf"), 0, false, false)
check(samefile("foo.lf", "foo_lf/foo"))

check(mtn("checkout", "--revision", revs.foofoo, "foo_foo_lf"), 0, false, false)
check(samefile("foofoo.lf", "foo_foo_lf/foo"))
