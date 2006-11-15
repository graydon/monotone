
mtn_setup()

mkdir("gongolo")

writefile("include.lua", 'include("../gongolo/aaa.rc")')

writefile("includedir.lua", 'includedir("../gongolo")')

writefile("includedirpattern.lua", 'includedirpattern("../gongolo","*.rc")')

-- write two files and check that they will be invoked in alphabetic order
check(get("aaa.rc", "gongolo/aaa.rc"))
check(get("bbb.zz", "gongolo/bbb.zz"))

-- setup a wrk dir
check(mtn("setup", "--branch=testbranch", "alt_wrk"), 0, false, false)

-- include directly a single file
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../include.lua", "status")), 0, true, false)
check(qgrep("BOOGA BOOGA", "stdout"))

-- include dir
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../includedir.lua", "status")), 0, true, false)
check(qgrep("BOOGA BOOGACICCA CICCA", "stdout"))

-- include dir with patterns
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../includedirpattern.lua", "status")), 0, true, false)
check(qgrep("BOOGA BOOGA", "stdout"))

-- write a third file: should be read between the two previous ones
check(get("aba.rc", "gongolo/aba.rc"))
check(indir("alt_wrk", mtn("--root=.", "--rcfile=../includedir.lua", "status")), 0, true, false)
check(qgrep("BOOGA BOOGAhu huCICCA CICCA", "stdout"))

check(indir("alt_wrk", mtn("--root=.", "--rcfile=../includedirpattern.lua", "status")), 0, true, false)
check(qgrep("BOOGA BOOGAhu hu", "stdout"))
