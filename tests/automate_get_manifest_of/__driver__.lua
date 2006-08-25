
mtn_setup()

check(get("expected"))
check(get("expected2"))

addfile("foo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev = base_revision()

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get_manifest_of", rev), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- should work even if we don't specify the manifest ID
check(mtn("automate", "get_manifest_of"), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- ensure that missing revisions fail
check(mtn("automate", "get_manifest_of", string.rep("0", 40)), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that revisions are not being completed
check(mtn("automate", "get_revision", string.sub(rev, 1, 30)), 1, true, false)
check(fsize("stdout") == 0)

-- check that modified working copy manifest is correct
writefile("foo", "bla bla\n")        
check(mtn("automate", "get_manifest_of"), 0, true, false)
canonicalize("stdout")
check(samefile("expected2", "stdout"))
