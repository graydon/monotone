
mtn_setup()

getfile("myhooks")
mkdir("fooxyzzybar")
check(mtn("--confdir=fooxyzzybar", "--rcfile=myhooks", "ls", "known"))
check(exists("fooxyzzybar/checkfile"))
