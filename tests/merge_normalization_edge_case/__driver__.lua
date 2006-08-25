
mtn_setup()

check(get("parent"))
check(get("left"))
check(get("right"))

parent = "fe24df7edf04cb06161defc10b252c5fa32bf1f7"
left = "f4657ce998dd0e39465a3f345f3540b689fd60ad"
right = "1836ed24710f5b8943bed224cf296689c6a106c2"

check(sha1("parent") == parent)
check(sha1("left") == left)
check(sha1("right") == right)

copy("parent", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("left", "stdin")
check(mtn("fload"), 0, false, false, true)
copy("right", "stdin")
check(mtn("fload"), 0, false, false, true)

check(mtn("fmerge", parent, left, right), 0, true, false)
canonicalize("stdout")
rename("stdout", "merge.monotone")

-- in this case the output should be the same as right.
check(samefile("merge.monotone", "right"))
