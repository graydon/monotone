
mtn_setup()

addfile("input.txt", "version 0 of the file")

-- can't use passphrase == keyname here, because
-- it's OK for the keyname to be logged.
check(get("hook.lua"))
pass = "xyzzypassphrasexyzzy"
check(mtn("genkey", "quux"), 0, false, false, string.rep(pass.."\n", 2))

check(mtn("--branch=testbranch", "--debug", "commit", "-m", "foo",
          "--rcfile=hook.lua", "-k", "quux"), 0, false, true)

check(not qgrep("xyzzypassphrasexyzzy", "stderr"))
