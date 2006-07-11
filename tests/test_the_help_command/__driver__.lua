
mtn_setup()

check(mtn("help", "mv"), 0, true, 0)
rename("stdout", "out")
check(mtn("--help", "mv"), 0, true, 0)
check(samefile("stdout", "out"))
