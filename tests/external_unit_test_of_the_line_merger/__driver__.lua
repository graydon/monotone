
skip_if(not existsonpath("diff3"))

mtn_setup()

check(get("left"))
check(get("right"))
check(get("ancestor"))

anc = "cec9ec2e479b700ea267e70feb5a4eb15155190d"
left = "52f65363d555fecd3d2e887a207c3add0a949638"
right = "3ea0b30aa5c7b20329ce9170ff4d379522c8bcda"

aver = sha1("ancestor")
lver = sha1("left")
rver = sha1("right")

check(aver == anc)
check(lver == left)
check(rver == right)

copy("ancestor", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("left", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("right", "stdin")
check(mtn("fload"), 0, false, false, true)

check(mtn("fmerge", anc, left, right), 0, true, false)
canonicalize("stdout")
rename("stdout", "merge.monotone")

-- we expect the output to be the same as the right file in this case
check(samefile("merge.monotone", "right"))
