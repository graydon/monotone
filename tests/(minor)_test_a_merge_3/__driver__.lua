
mtn_setup()

-- This test is a bug report.

-- This is a real merge error -- it should be a clean merge, but it
-- produces a conflict.

check(get("ancestor"))
check(get("left"))
check(get("right"))

anc = "4f7cfb26927467e9f2a37070edbb19785cbb2f2d"
left = "adc1ca256e9313dd387448ffcd5cf7572eb58d8e"
right = "63ad35cd3955bfa681b76b31d7f2fd745e84f654"

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

check(samefile("merge.diff3", "merge.monotone"), 0, false, false)
