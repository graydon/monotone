
mtn_setup()

check(get("left"))
check(get("right"))

copy("left", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("right", "stdin")
check(mtn("fload"), 0, false, false, true)
left = sha1("left")
right = sha1("right")
check(mtn("fmerge", left, left, right), 0, true, false)
canonicalize("stdout")
check(samefile("right", "stdout"))
