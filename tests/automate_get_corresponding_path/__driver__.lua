
mtn_setup()

check(get("expected"))
check(get("expected2"))

-- add a file, commit, rename, commit, find the old name
addfile("foo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
old_rev = base_revision()
check(mtn("rename", "--bookkeep-only", "foo", "foo2"), 0, true, false)
rename("foo", "foo2")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev = base_revision()

-- file foo has been renamed to foo2, in the new revision we should be able to find the old name
check(mtn("automate", "get_corresponding_path", rev, "foo2", old_rev), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- check that it works for files that don't exist in the previous revision
addfile("foo3", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev = base_revision()
check(mtn("automate", "get_corresponding_path", rev, "foo3", old_rev), 0, true, false)
canonicalize("stdout")
check(samefile("expected2", "stdout"))

-- TODO: accidental clean merge test
