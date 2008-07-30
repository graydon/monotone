
include("common/netsync.lua")
mtn_setup()
netsync.setup_with_notes()

-- This tests *some* of the netsync error code handling. It can't test
-- reporting of interrupted connections (21x) because we don't have a
-- reliable way to kill one side halfway through the connection,
-- and can't test 5xx errors because those require that one side isn't
-- speaking the protocol correctly.

function get_errcode(who)
   canonicalize("testnotes-" .. who .. ".log")
   local dat = readfile("testnotes-" .. who .. ".log")
   local _, _, errcode = string.find(dat, "\n%d+ end: status = (%d%d%d)\n")
   L("Error code for ", who, " is ", errcode)
   if errcode == nil then errcode = "<missing>" end
   return errcode
end

function chk_errcode_is(errcode, which)
   L("Want error code ", errcode)
   errcode = string.gsub(errcode, "x", ".")
   if which == nil then
      local srvcode = get_errcode("server")
      local clicode = get_errcode("client")
      check(string.find(srvcode, errcode) ~= nil)
      check(string.find(clicode, errcode) ~= nil)
   else
      local code = get_errcode(which)
      check(string.find(code, errcode) ~= nil)
   end
end

function clearnotes()
   check(remove("testnotes-client.log"))
   check(remove("testnotes-server.log"))
end


addfile("testfile", "file contents")
commit()
addfile("otherfile", "other contents")
commit("otherbranch")

netsync.sync("testbranch")
chk_errcode_is(200)
clearnotes()

writefile("denyread", "function get_netsync_read_permitted() return false end")
srv = netsync.start({"--rcfile=denyread"})
srv:sync({"otherbranch"}, 2, 1)
srv:stop()
chk_errcode_is(412)
clearnotes()

check(mtn2("genkey", "unknown@tester.net"), 0, false, false, string.rep("unknown@tester.net\n", 2))
srv = netsync.start()
srv:sync({"testbranch", "--key=unknown@tester.net"}, 2, 1)
srv:stop()
chk_errcode_is(412) -- anonymous write (was 422 unknown key)
clearnotes()

check(mtn("db", "set_epoch", "testbranch", string.rep("0", 40)))
check(mtn2("db", "set_epoch", "testbranch", string.rep("1", 40)))
srv = netsync.start()
srv:push({"testbranch"}, 2, 1)
srv:stop()
chk_errcode_is(432)
clearnotes()
