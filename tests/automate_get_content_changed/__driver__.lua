
mtn_setup()

check(get("expected"))

-- trivial case; a single file, with no history (it should be marked itself)
addfile("foo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev = base_revision()

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get_content_changed", rev, "foo"), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- TODO: accidental clean merge test
