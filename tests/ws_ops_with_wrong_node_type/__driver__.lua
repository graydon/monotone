mtn_setup()

addfile("file", "asdf")
mkdir("dir")
check(mtn("add", "dir"), 0, false, false)
commit()

check(mtn("automate", "get_current_revision"), 0, true, false)
rename("stdout", "orig_rev")

-- fail to move a dir under a file
check(mtn("rename", "--bookkeep-only", "dir", "file/subdir"), 1, false, false)
check(mtn("automate", "get_current_revision"), 0, true, false)
check(samefile("stdout", "orig_rev"))

-- running a recursive add what's supposed to be a file, but is actually a
-- dir...
mkdir("dir2")
check(mtn("rename", "--bookkeep-only", "file", "dir2"), 0, false, false)
-- should now be already added (this is a soft error now.  see stderr to
-- ensure proper response
check(mtn("add", "dir2"), 0, false, true)
check(qgrep("skipping dir2, already accounted for in workspace", "stderr"))
-- should have happened already in the --bookkeep-only version above.
check(mtn("rename", "file", "dir2"), 1, false, false)
