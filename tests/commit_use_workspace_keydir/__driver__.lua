-- Test that commit uses keydir from _MTN/options
-- It didn't in 0.39

mtn_setup()

-- adapted from lua-testsuite.lua 'mtn'
function mtn_default_keydir(...)
  -- Return mtn command string that uses default keydir. Specifing
  -- --confdir also sets --keydir, so don't specify either.
  if monotone_path == nil then
    monotone_path = os.getenv("mtn")
    if monotone_path == nil then
      err("'mtn' environment variable not set")
    end
  end
  return {monotone_path, "--no-default-confdir",
     "--norc", "--root=" .. test.root, "--db", "test.db",
  	  "--rcfile", test.root .. "/test_hooks.lua",
          "--key=tester@test.net", unpack(arg)}
end

addfile("randomfile", "random stuff")

-- this should find the right key in the keydir specified by _MTN/options
commit("testbranch", "blah-blah", mtn_default_keydir)

-- end of file

