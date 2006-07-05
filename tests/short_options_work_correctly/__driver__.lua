--  -*- Autoconf -*-

-- a test for the abbreviate options (-b, -k, -r, -m)

mtn_setup()

writefile("maude", "the file maude")

writefile("liver", "the file liver")

-- same as mtn() but without --db and --key
function short_mtn(...)
  return raw_mtn("--rcfile", test.root.."/test_hooks.lua",
                 "--nostd", "--norc", "--root", test.root,
                 "--keydir", test.root.."/keys", unpack(arg))
end

check(short_mtn("add", "maude"), 0, false, false)

-- check it won't work with a bad key
check(short_mtn("-k", "badkey@example.com", "-b", "test.branch", "commit",
                "-d", "test.db", "-m", "happy"), 1, false, false)

-- the failed log will have been saved
remove("_MTN/log")

-- and it does work with a key
check(short_mtn("-k", "tester@test.net", "-b", "test.branch", "commit",
                "-d", "test.db", "-m", "happy"), 0, false, false)
