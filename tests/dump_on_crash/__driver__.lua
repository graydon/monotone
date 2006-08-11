
mtn_setup()

check(mtn("crash", "I"), 3, false, false)
check(exists("_MTN/debug"))

check(remove("_MTN"))
check(mtn("crash", "I"), 3, false, false)
check(exists("dump"))

mkdir("try")
check(mtn("crash", "I", "--confdir=try"), 3, false, false)
check(exists("try/dump"))

check(mtn("crash", "I", "--dump=fork"), 3, false, false)
check(exists("fork"))
