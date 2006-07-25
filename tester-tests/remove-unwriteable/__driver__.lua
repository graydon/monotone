
skip_if(not existsonpath("chmod"))

mkdir("foo")
writefile("foo/bar", "quux")

check({"chmod", "a-w", "foo"})
check({"chmod", "a-w", "foo/bar"})
remove("foo")
check(not exists("foo"))
