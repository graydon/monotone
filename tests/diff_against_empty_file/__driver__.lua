mtn_setup()

check(get("expected.diff"))

addfile("foo", "1\n2\n")
commit("foo", "foo")

rename("foo", "foo.away")
writefile("foo")
check(mtn("diff", "foo"), 0, true)
canonicalize("stdout")
rename("stdout", "monodiff")

check(samefile("monodiff", "expected.diff"))
