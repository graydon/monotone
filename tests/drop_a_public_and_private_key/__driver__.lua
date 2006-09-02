
mtn_setup()

check(mtn("genkey", "john@doe.com"), 0, false, false, string.rep("john@doe.com\n", 2))

check(mtn("privkey", "john@doe.com"), 0, true, false)

check(qgrep('john@doe.com', "stdout"))

check(mtn("dropkey", "john@doe.com"), 0, false, false)

check(mtn("privkey", "john@doe.com"), 1, false, true)

check(qgrep('do not exist', "stderr"))

check(mtn("pubkey", "john@doe.com"), 1, false, true)

check(qgrep('does not exist', "stderr"))
