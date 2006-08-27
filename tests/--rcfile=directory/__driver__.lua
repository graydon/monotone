
mtn_setup()

mkdir("gongolo")

-- write two files and check that they will be invoked in alphabetic order
check(get("aaa.rc", "gongolo/aaa.rc"))
check(get("bbb.rc", "gongolo/bbb.rc"))

-- note: rcfile is placed outside workdir
check(mtn("setup", "--branch=testbranch", "alt_wrk"), 0, false, false)
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../gongolo", "status")), 0, true, false)
check(qgrep("BOOGA BOOGACICCA CICCA", "stdout"))

-- write a third file: should be read beetween the two previous ones
check(get("aba.rc", "gongolo/aba.rc"))
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../gongolo", "status")), 0, true, false)
check(qgrep("BOOGA BOOGAhu huCICCA CICCA", "stdout"))
