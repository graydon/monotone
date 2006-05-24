
mtn_setup()

tkey = "happy@bogus.com"

-- fail to enter a passphrase
check(cmd(mtn("genkey", tkey)), 1, false, false)

-- fail to enter passphrase twice
check(cmd(mtn("genkey", tkey)), 1, false, false, tkey .. "\n")

-- generate a new key
check(cmd(mtn("genkey", tkey)), 0, false, false, tkey .. "\n" .. tkey .. "\n")

-- check key exists
check(cmd(mtn("ls", "keys")), 0, true)
check(cmd(grep(tkey, "stdout")), 0, false)

-- check globbing on name works
check(cmd(mtn("ls", "keys", "happy*")), 0, true)
check(cmd(grep(tkey, "stdout")), 0, false)

-- check globbing on bogus name misses key
check(cmd(mtn("ls", "keys", "burp*")), 0, true, false)
check(cmd(qgrep(tkey, "stdout")), 1)


-- second section, check making certs with this key

writefile("input.txt", "blah blah blah\n")

check(cmd(mtn("add", "input.txt")), 0, false, false)
check(cmd(commit()), 0, false, false)
tsha = base_revision()
check(cmd(mtn("--key", tkey, "cert", tsha, "color", "pink")), 0, false, false)
check(cmd(mtn("ls", "certs", tsha)), 0, true)
check(cmd(qgrep("pink", "stdout")))

check(cmd(mtn("--key", tkey, "cert", tsha, "color")), 0, false, false, "yellow\n")
check(cmd(mtn("ls", "certs", tsha)), 0, true, false)
check(cmd(qgrep("pink", "stdout")))
check(cmd(qgrep("yellow", "stdout")))

-- third section, keys with a + in the user portion work, keys with a
-- + in the domain portion don't work.
goodkey = "test+thing@example.com"

check(cmd(mtn("genkey", goodkey)), 0, false, false, string.rep(goodkey .. "\n", 2))
--exists
check(cmd(mtn("ls", "keys")), 0, true)
-- remember '+' is a special character for regexes
check(cmd(qgrep(string.gsub(goodkey, "+", "\\+"), "stdout")))

-- bad keys fail
badkey1 = "test+thing@example+456.com"
check(cmd(mtn("genkey", badkey1)), 1, false, false, string.rep(badkey1 .. "\n", 2))
badkey2 = "testthing@example+123.com"
check(cmd(mtn("genkey", badkey2)), 1, false, false, string.rep(badkey2 .. "\n", 2))

-- fourth section, keys with all supported characters (for the user portion)
-- in the user portion work, keys with the same in the domain portion don't
-- work.
goodkey = "test_a_+thing.ie@example.com"

check(cmd(mtn("genkey", goodkey)), 0, false, false, string.rep(goodkey .. "\n", 2))
--exists
check(cmd(mtn("ls", "keys")), 0, true)
check(cmd(qgrep(string.gsub(goodkey, "+", "\\+"), "stdout")))

-- bad keys fail
badkey1 = "test_a_+thing.ie@exa_m+p.le.com"
check(cmd(mtn("genkey", badkey1)), 1, false, false, string.rep(badkey1 .. "\n", 2))
badkey2 = "testthing@exa_m+p.le123.com"
check(cmd(mtn("genkey", badkey2)), 1, false, false, string.rep(badkey2 .. "\n", 2))
