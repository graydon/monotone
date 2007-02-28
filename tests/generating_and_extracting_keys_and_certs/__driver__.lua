
mtn_setup()

tkey = "happy@bogus.com"

-- empty passphrase
check(mtn("genkey", tkey .. ".empty"), 0, false, false)

-- fail to enter passphrase 3 times
check(mtn("genkey", tkey), 1, false, false, tkey .. "\n" .. "\n" .. "\n" .. tkey .. "\n" .. tkey .. "\n")

-- generate a new key
check(mtn("genkey", tkey), 0, false, false, tkey .. "\n" .. tkey .. "\n")

-- check key exists
check(mtn("ls", "keys"), 0, true)
check(qgrep(tkey, "stdout"))

-- check globbing on name works
check(mtn("ls", "keys", "happy*"), 0, true)
check(qgrep(tkey, "stdout"))

-- check globbing on bogus name misses key
check(mtn("ls", "keys", "burp*"), 0, true, false)
check(not qgrep(tkey, "stdout"))


-- second section, check making certs with this key

writefile("input.txt", "blah blah blah\n")

check(mtn("add", "input.txt"), 0, false, false)
commit()
tsha = base_revision()
check(mtn("--key", tkey, "cert", tsha, "color", "pink"), 0, false, false)
check(mtn("ls", "certs", tsha), 0, true)
check(qgrep("pink", "stdout"))

check(mtn("--key", tkey, "cert", tsha, "color"), 0, false, false, "yellow\n")
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep("pink", "stdout"))
check(qgrep("yellow", "stdout"))

-- third section, keys with a + in the user portion work, keys with a
-- + in the domain portion don't work.
goodkey = "test+thing@example.com"

check(mtn("genkey", goodkey), 0, false, false, string.rep(goodkey .. "\n", 2))
--exists
check(mtn("ls", "keys"), 0, true)
-- remember '+' is a special character for regexes
check(qgrep(string.gsub(goodkey, "+", "\\+"), "stdout"))

-- bad keys fail
badkey1 = "test+thing@example+456.com"
check(mtn("genkey", badkey1), 1, false, false, string.rep(badkey1 .. "\n", 2))
badkey2 = "testthing@example+123.com"
check(mtn("genkey", badkey2), 1, false, false, string.rep(badkey2 .. "\n", 2))

-- fourth section, keys with all supported characters (for the user portion)
-- in the user portion work, keys with the same in the domain portion don't
-- work.
goodkey = "test_a_+thing.ie@example.com"

check(mtn("genkey", goodkey), 0, false, false, string.rep(goodkey .. "\n", 2))
--exists
check(mtn("ls", "keys"), 0, true)
check(qgrep(string.gsub(goodkey, "+", "\\+"), "stdout"))

-- bad keys fail
badkey1 = "test_a_+thing.ie@exa_m+p.le.com"
check(mtn("genkey", badkey1), 1, false, false, string.rep(badkey1 .. "\n", 2))
badkey2 = "testthing@exa_m+p.le123.com"
check(mtn("genkey", badkey2), 1, false, false, string.rep(badkey2 .. "\n", 2))
