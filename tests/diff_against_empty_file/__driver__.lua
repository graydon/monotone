
skip_if(not existsonpath("patch"))
mtn_setup()

addfile("foo", "1\n2\n")
commit("foo", "foo")

rename("foo", "foo.away")
writefile("foo")
check(mtn("diff", "foo"), 0, true)
rename("stdout", "monodiff")

-- see whether the patch is well-formed
check({"patch", "-p0", "-R"}, 0, false, false, {"monodiff"})

-- see whether the resulting file is the same as the original one
check(samefile("foo", "foo.away"))
