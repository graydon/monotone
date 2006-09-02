
mtn_setup()

-- this is an old privkey generated with 0.23

check(get("old_privkey", "pkt"))

check(mtn("read", "pkt"), 0, false, true)
check(qgrep("read 1 packet", "stderr"))
check(mtn("ls", "keys"), 0, true)
check(qgrep("foo@bar.com", "stdout"))

addfile("foo", "foo")

-- check that we can use the key we just read
-- if it imported wrong, it'll fail by not accepting the passphrase

check(mtn("ci", "-bfoo", "-mbar"), 0, false, false, string.rep("foo@bar.com\n", 2))
