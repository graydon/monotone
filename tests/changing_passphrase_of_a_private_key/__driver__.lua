
mtn_setup()

tkey = "happy@bogus.com"

-- generate a new key
check(mtn("genkey", tkey), 0, false, false, string.rep(tkey.."\n", 2))

-- fail to enter any passphrase
check(mtn("passphrase", tkey), 1, false, false)

-- fail to give correct old passphrase
check(mtn("passphrase", tkey), 1, false, false, string.rep("bad\n", 3))

-- fail to repeat new password
check(mtn("passphrase", tkey), 1, false, false, tkey.."\n"..tkey.."-new\nbad\n\nnew\nbad")

-- change the passphrase successfully
check(mtn("passphrase", tkey), 0, false, false, tkey.."\n"..string.rep(tkey.."-new\n", 2))

-- check that the passphrase changed
check(mtn("passphrase", tkey), 0, false, false, tkey.."-new\n"..string.rep(tkey.."\n",2))
