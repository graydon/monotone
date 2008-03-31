mtn_setup()

-- adapted from lua-testsuite.lua mtn; no --confdir, --keydir, or --key
function pure_mtn(...)
  if monotone_path == nil then
    monotone_path = os.getenv("mtn")
    if monotone_path == nil then
      err("'mtn' environment variable not set")
    end
  end
  return {monotone_path, "--ssh-sign=no", "--norc", "--root=" .. test.root, "--db", "test.db",
  	  "--rcfile", test.root .. "/test_hooks.lua", unpack(arg)}
end

-- this should find a private key in the specified keydir

-- On Unix, the return code from the background process is the signal
-- value; srv:finish sends signal -15 to stop the process. Not so on
-- Windows.

-- However, this means we need to check stderr from the background
-- command to check for success or failure.

if ostype == "Windows" then
expected_ret = 1
else
expected_ret = -15
end

-- srv = bg(pure_mtn("serve", "--confdir="..test.root, "--keydir="..test.root.."/keys"), expected_ret, false, true)
-- sleep(2)
-- srv:finish()
-- check(qgrep("beginning service", "stderr"))

-- this should find a private key in the keys directory under the specified confdir

-- srv = bg(pure_mtn("serve", "--confdir="..test.root), expected_ret, false, true)
-- sleep(2)
-- srv:finish()
-- check(qgrep("beginning service", "stderr"))

-- this should fail to decrypt the private key found in ~/.monotone/keys

-- however before get_default_keydir was added to the key_dir option in
-- options_list.hh it would hit an invariant on an empty key_dir for any
-- CMD_NO_WORKSPACE that attempted to call get_user_key(...)

-- The expected return value is the same on Unix and Windows

mkdir(test.root.."/empty")
-- FIXME: this should probably be set globally in lua-testsuite.lua for
--        all tests.
set_env("HOME", test.root.."/empty")
srv = bg(pure_mtn("serve"), 1, false, true)
sleep(2)
srv:finish()
check(qgrep("misuse: you have no private key", "stderr"))

-- end of file
