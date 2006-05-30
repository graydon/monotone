
skip_if(not existsonpath("diff3"))

mtn_setup()

getfile("left")
getfile("right")
getfile("ancestor")

anc = "cec9ec2e479b700ea267e70feb5a4eb15155190d"
left = "52f65363d555fecd3d2e887a207c3add0a949638"
right = "3ea0b30aa5c7b20329ce9170ff4d379522c8bcda"

aver = sha1("ancestor")
lver = sha1("left")
rver = sha1("right")

check(aver == anc)
check(lver == left)
check(rver == right)

copyfile("ancestor", "stdin")
check(cmd(mtn("fload")), 0, false, false, true)
copyfile("left", "stdin")
check(cmd(mtn("fload")), 0, false, false, true)
copyfile("right", "stdin")
check(cmd(mtn("fload")), 0, false, false, true)

check(cmd(mtn("fmerge", anc, left, right)), 0, true, false)
canonicalize("stdout")
rename("stdout", "merge.monotone")

-- we expect the output to be the same as the right file in this case
check(samefile("merge.monotone", "right"))
