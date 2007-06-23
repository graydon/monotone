
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("a", "b")
check(mtn2("commit", "-m", "foo"), 0, false, false)


srv2 = netsync.start(3)

get("server1.rc")
rcdata = readfile("server1.rc")
rcdata = string.gsub(rcdata, "localhost:12345", srv2.address)
writefile("server1.rc", rcdata)

srv1 = netsync.start({"--rcfile=server1.rc"})

srv1:push({"*"})

sleep(5)

srv1:stop()
srv2:stop()

-- should now exist in mtn3
check(mtn3("update"), 0, false, false)
