
mtn_setup()

get("myhooks")
mkdir("fooxyzzybar")
check(mtn("--confdir=fooxyzzybar", "--rcfile=myhooks", "ls", "known"))
check(exists("fooxyzzybar/checkfile"))
