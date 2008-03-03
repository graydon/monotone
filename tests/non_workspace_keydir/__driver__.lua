mtn_setup()

-- adapted from lua-testsuite.lua
function pure_mtn(...)
  if monotone_path == nil then
    monotone_path = os.getenv("mtn")
    if monotone_path == nil then
      err("'mtn' environment variable not set")
    end
  end
  return {monotone_path, "--norc", "--root=" .. test.root, "--db", "test.db", 
  	  "--rcfile", test.root .. "/test_hooks.lua", unpack(arg)}
end

-- this should find a private key in the specified keydir
-- presumably the -15 return code comes from the kill -15 in the finish function in testlib.lua

srv = bg(pure_mtn("serve", "--confdir="..test.root, "--keydir="..test.root.."/keys"), -15, false, true)
sleep(2)
srv:finish()

-- this should find a private key in the keys directory under the specified confdir
-- presumably the -15 return code comes from the kill -15 in the finish function in testlib.lua

srv = bg(pure_mtn("serve" ,"--confdir="..test.root), -15, false, true)
sleep(2)
srv:finish()

-- this should fail to decrypt the private key found in ~/.monotone/keys

-- however before get_default_keydir was added to the key_dir option in
-- options_list.hh it would hit an invariant on an empty key_dir for any
-- CMD_NO_WORKSPACE that attempted to call get_user_key(...)

srv = bg(pure_mtn("serve"), 1, false, true)
sleep(2)
srv:finish()
