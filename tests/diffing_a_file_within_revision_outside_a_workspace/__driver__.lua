
mtn_setup()

writefile("foo1", "foo file 1")
writefile("foo2", "foo file 2")

check(mtn("add", "foo1"), 0, false, false)
commit()
parent = base_revision()

check(mtn("add", "foo2"), 0, false, false)
commit()
second = base_revision()

remove("_MTN")
check(mtn("diff", "--revision", parent, "--revision", second), 0, false, false)
-- check it works when specifying files
check(mtn("diff", "--revision", parent, "--revision", second, "foo2"), 0, false, false)

-- should work without any --root argument, too.  we do this in a
-- special temporary directory to ensure no risk to a higher-level
-- workspace.

tmpdir = make_temp_dir()
copy(test.root .. "/test.db", tmpdir)
copy(test.root .. "/keys", tmpdir)

check(indir(tmpdir, 
	    { monotone_path, "--norc",
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second }),
      0, false, false)

check(indir(tmpdir, 
	    { monotone_path, "--norc", "--root="..tmpdir,
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second }),
      0, false, false)

check(indir(tmpdir, 
	    { monotone_path, "--norc", "--root=.",
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second }),
      0, false, false)

check(indir(tmpdir, 
	    { monotone_path, "--norc",
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second, "foo2" }),
      0, false, false)

check(indir(tmpdir, 
	    { monotone_path, "--norc", "--root="..tmpdir,
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second, "foo2" }),
      0, false, false)

check(indir(tmpdir, 
	    { monotone_path, "--norc", "--root=.",
	       "--db="..tmpdir.."/test.db",
	       "--confdir="..tmpdir,
	       "--keydir="..tmpdir.."/keys",
	       "diff", "--revision", parent, "--revision", second, "foo2" }),
      0, false, false)

remove(tmpdir)
