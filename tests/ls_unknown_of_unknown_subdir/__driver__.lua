mtn_setup()

mkdir("foo")
writefile("foo/a", "aaa")
writefile("foo/b", "bbb")

-- this fails complaining that foo is an unknown path

xfail_if(true, mtn("ls", "unknown", "foo"), 0)
