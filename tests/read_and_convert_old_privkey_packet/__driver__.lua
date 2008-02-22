
mtn_setup()

-- this is an old privkey generated with 0.23
-- mtn read should accept it and convert it to the new format

check(get("old_privkey", "pkt"))
check(mtn("read", "pkt"), 0, false, true)
check(qgrep("read 1 packet", "stderr"))
check(qgrep("keypair", "keys/foo@bar.com"))
check(not qgrep("privkey", "keys/foo@bar.com"))

check(mtn("ls", "keys"), 0, true)
check(qgrep("foo@bar.com", "stdout"))

addfile("foo", "foo")

-- if we put the old privkey in the keydir, it should get
-- auto-converted the first time anything tries to read it

check(get("old_privkey", "keys/foo@bar.com"))
check(mtn("ls", "keys"), 0, true, true, "foo@bar.com\n")
check(qgrep("foo@bar.com", "stdout"))
check(qgrep("converting old-format", "stderr"))
check(qgrep("keypair", "keys/foo@bar.com"))
check(not qgrep("privkey", "keys/foo@bar.com"))


-- check that we can use the converted key to commit with

-- 1) without a --key= switch, it should give us the multiple
--    available keys message and error out
remove("_MTN/options")
check(nokey_mtn("ci", "-bfoo", "-mbar"), 1, nil, true)
check(qgrep("multiple private keys", "stderr"))

-- 2) --key=foo@bar.com should work

remove("_MTN/options")
check(nokey_mtn("ci", "-bfoo", "-mbar", "--key=foo@bar.com"), 0, nil, true)
check(qgrep("committed revision", "stderr"))

-- 3) that should have actually signed the certs with that key
check(mtn("ls", "certs", "h:foo"), 0, true, false)
check(qgrep("Key *: *foo@bar\\.com", "stdout"))
check(not qgrep("Key *: *tester@test\\.net", "stdout"))
