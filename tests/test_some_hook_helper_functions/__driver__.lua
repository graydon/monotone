
mtn_setup()

get("testhooks")

check(raw_mtn("--rcfile=testhooks", "ls", "unknown"), 0, false, false)
check(exists("outfile"))
