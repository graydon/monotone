
mtn_setup()

-- with no database should work
check(raw_mtn("--keydir=keys", "genkey", "foobar"), 0, false, false,
      string.rep("foobar\n", 2))

check(raw_mtn("--keydir=keys", "passphrase", "foobar"), 0, false, false,
      "foobar\n"..string.rep("barfoo\n", 2))

check(raw_mtn("--keydir=keys", "ls", "keys"), 0, false, false)

check(raw_mtn("--keydir=keys", "pubkey", "foobar"), 0, false, false)

check(raw_mtn("--keydir=keys", "dropkey", "foobar"), 0, false, false)

-- with an invalid database should fail
check(raw_mtn("--keydir=keys", "--db=bork", "genkey", "foo@baz"), 1, false, false, string.rep("foo@baz\n", 2))
