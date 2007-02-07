
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

pubkey = "52f32ec62128ea3541ebdd9d17400e268cfcd3fe"
privkey = "06b040c37796863b53f10dc23fcccf379cc2e259"

check(get("newkeys.txt", "stdin"))
check(mtn("read"), 0, false, false, true)

-- First commit a version that doesn't use the new key, and make sure
-- that it doesn't get transferred.
addfile("testfile", "version 0 of test file")
commit("testbranch")

netsync.pull("testbranch")

check(mtn2("ls", "keys"), 0, true, false)
check(not qgrep(pubkey, "stdout"))
check(not qgrep(privkey, "stdout"))

-- Now check that --key-to-push works.
srv = netsync.start(2)
check(mtn("--rcfile=netsync.lua", "push", srv.address,
          "testbranch", "--key-to-push=foo@test.example.com"),
      0, false, false)
srv:finish()
check(mtn2("dropkey", "foo@test.example.com"), 0, false, false)

-- Now commit a version that does use the new key, and make sure that
-- now it does get transferred.
writefile("testfile", "version 1 of test file")
check(mtn("--branch=testbranch", "--message=blah-blah",
          "--key=foo@test.example.com", "commit"), 0, false, false)

netsync.pull("testbranch")

check(mtn2("ls", "keys"), 0, true, false)
check(qgrep(pubkey, "stdout"))
check(not qgrep(privkey, "stdout"))
