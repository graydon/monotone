
include("common/netsync.lua")
mtn_setup()
netsync.setup()
rseed = get_pid()

addfile("testfile", "foo")
commit()
t1 = base_revision()

-- set defaults in db 2
math.randomseed(rseed)
netsync.pull("testbranch")
check(mtn2("automate", "get_revision", t1), 0, false, false)

writefile("testfile", "blah blah")
commit()
t2 = base_revision()

-- make sure the defaults really were set to 'testbranch'
math.randomseed(rseed)
srv = netsync.start()
check(mtn2("pull"), 0, false, false)
srv:stop()
check(mtn2("automate", "get_revision", t2), 0, false, false)

-- do a --set-default pull of another branch
math.randomseed(rseed)
srv = netsync.start()
srv:pull({"otherbranch", "--set-default"})
srv:stop()

-- create a new revision on that branch
writefile("testfile", "other1")
commit("otherbranch")
o1 = base_revision()

-- and make sure that our default is now it
math.randomseed(rseed)
srv = netsync.start()
check(mtn2("pull"), 0, false, false)
srv:stop()

check(mtn2("automate", "get_revision", o1), 0, false, false)
