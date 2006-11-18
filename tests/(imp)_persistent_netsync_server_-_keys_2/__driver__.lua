
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

-- This tests a somewhat subtle version of how public keys are handled
-- with netsync.  The rule is, we serve a key if we're serving any cert
-- signed by that key.  The question is, if when we boot up the server
-- we already have the key in question, but it isn't signing any such
-- certs, but _then we get pushed such a cert_, do we push out the key
-- on further netsyncs?

-- We create a key in database 2, read the public key into database 1,
-- then start database 1 serving.  Then we push a cert into database 1
-- signed by the key, and we pull into database 3.

check(mtn2("genkey", "foo@bar"), 0, false, false, string.rep("foo@bar\n", 2))
check(mtn2("pubkey", "foo@bar"), 0, true)
rename("stdout", "stdin")
check(mtn("read"), 0, false, false, true)

addfile("foo", "data data blah")
check(mtn2("status"), 0, false, false)
check(mtn2("commit", "--key=foo@bar", "--branch=testbranch", "-m", 'commit foo'), 0, false, false)

srv = netsync.start()
srv:push("testbranch", 2)
srv:pull("testbranch", 3)
srv:finish()

check(mtn3("ls", "keys"), 0, true, false)
check(qgrep("foo@bar", "stdout"))
