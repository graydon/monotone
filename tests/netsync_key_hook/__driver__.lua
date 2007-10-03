
include("common/netsync.lua")
mtn_setup()
netsync.setup()

writefile("foo", "bar")
check(mtn2("add", "foo"), 0, false, false)
check(mtn2("commit", "-mx"), 0, false, false)

check(mtn("genkey", "badkey@test.net"), 0,
      false, false, string.rep("badkey@test.net\n",2))

get("read-permissions")
get("client-hooks.lua")

srv = netsync.start(2)

-- We don't want the --key argument, so we have to do this ourselves.
function client(what, ret)
  args = {"--rcfile=netsync.lua", "--rcfile=test_hooks.lua",
     "--keydir=keys",
     "--db=test.db", srv.address,
     "--rcfile=client-hooks.lua",
     "*"}
  for k, v in pairs(args) do
     table.insert(what, v)
  end
  check(raw_mtn(unpack(what)), ret, false, false)
end

client({"push"}, 0)
client({"pull"}, 0)
client({"sync"}, 0)
client({"pull", "--key=badkey@test.net"}, 1)

srv:stop()
