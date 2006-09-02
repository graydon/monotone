
mtn_setup()

check(get("pubkey.txt", "stdin"))

check(mtn("read"), 0, false, false, true)

check(mtn("pubkey", "john@doe.com"), 0, true, false)

check(qgrep('john@doe.com', "stdout"))

check(mtn("dropkey", "john@doe.com"), 0, false, false)

check(mtn("pubkey", "john@doe.com"), 1, false, true)

check(qgrep('does not exist', "stderr"))
