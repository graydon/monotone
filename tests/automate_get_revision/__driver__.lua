
mtn_setup()

check(get("expected"))

addfile("foo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
base = base_revision()

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get_revision", base), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- should error out if we don't specify the revision
check(mtn("automate", "get_revision"), 1, false, false)

-- ensure that missing revisions fail
check(mtn("automate", "get_file", string.rep("0", 40)), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that revisions are not being completed
check(mtn("automate", "get_file", string.sub(base, 1, 30)), 1, true, false)
check(fsize("stdout") == 0)
