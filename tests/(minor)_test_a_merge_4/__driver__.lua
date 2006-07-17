
mtn_setup()

-- This test is a bug report.

-- This is a real merge error -- it should be a clean merge, but it
-- produces a conflict.

check(get("ancestor"))
check(get("left"))
check(get("right"))

anc = "a2c50da63f01b242d8aaeb34d65e48edf0fef21b"
left = "8d5a2273e0e3da4aa55ff731e7152a673b63f08a"
right = "6745b398ffecec36bc4fc45598e678b3391d91b2"

check(anc == sha1("ancestor"))
check(left == sha1("left"))
check(right == sha1("right"))

copy("ancestor", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("left", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("right", "stdin")
check(mtn("fload"), 0, false, false, true)

check(get("merge.diff3"))

xfail_if(true, mtn("fmerge", anc, left, right), 0, true, false)
rename("stdout", "merge.monotone")

check(samefile("merge.diff3", "merge.monotone"))
