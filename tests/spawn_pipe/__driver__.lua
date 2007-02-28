
mtn_setup()

check(get("testhooks"))

check(raw_mtn("--rcfile=testhooks", "ls", "unknown"), 0, false, false)

skip_if(exists("skipfile"))
xfail_if(ostype == "Windows", exists("outfile"))
