
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "foo bar")
commit()

check(mtn("genkey", "foo@bar"), 0, false, false, "foo@bar\nfoo@bar\n")
check(mtn("pubkey", "foo@bar"), 0, true, false)
canonicalize("stdout")
copy("stdout", "foo_public")

srv = netsync.start({"--key=foo@bar"})
srv:pull("testbranch")
srv:stop()

check(mtn2("pubkey", "foo@bar"), 0, true, false)
canonicalize("stdout")
check(samefile("foo_public", "stdout"))
