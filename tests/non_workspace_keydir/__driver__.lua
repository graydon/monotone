-- adapted from lua-testsuite.lua mtn; no --confdir, --keydir, or --key
function pure_mtn(...)
  if monotone_path == nil then
    monotone_path = os.getenv("mtn")
    if monotone_path == nil then
      err("'mtn' environment variable not set")
    end
  end
  return {monotone_path, "--ssh-sign=no", "--norc",
	  "--root=" .. test.root, "--db", "test.db",
	  "--rcfile", test.root .. "/test_hooks.lua", unpack(arg)}
end

mtn_setup()

addfile("file", "contents")
commit()
rev = base_revision()

-- make test.root not a workspace anymore
check(rename("_MTN", "not_MTN"))

-- this should fail to find any private key to sign the cert with
check(pure_mtn("cert", rev, "fail1", "value"),
      1, nil, true)
check(qgrep("you have no private key", "stderr"))


-- this should find a private key in the keys directory under the
-- specified confdir
check(pure_mtn("--confdir="..test.root,
	       "cert", rev, "test1", "value"),
      0, nil, nil)

-- this should fail to find a private key, since there is no
-- keys subdirectory of this directory
check(pure_mtn("--confdir="..test.home,
	       "cert", rev, "fail2", "value"),
      1, nil, true)
check(qgrep("you have no private key", "stderr"))

-- this should find a private key in the specified keydir
check(pure_mtn("--confdir="..test.home, "--keydir="..test.root.."/keys",
	       "cert", rev, "test2", "value"),
      0, nil, nil)
