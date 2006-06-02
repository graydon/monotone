
mtn_setup()

getfile("left")
getfile("right")

copyfile("left", "stdin")
check(cmd(mtn("fload")), 0, false, false, true)
copyfile("right", "stdin")
check(cmd(mtn("fload")), 0, false, false, true)
left = sha1("left")
right = sha1("right")
check(cmd(mtn("fmerge", left, left, right)), 0, true, false)
canonicalize("stdout")
check(samefile("right", "stdout"))
