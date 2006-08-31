-- this is very similar to the explicit_merge test, except that the
-- result of the merge winds up in the current workspace.

mtn_setup()

check(get("testfile"))
check(get("otherfile"))
addfile("testfile")
addfile("otherfile")
commit()
anc = base_revision()

check(get("dont_merge"))
check(get("left"))
check(get("right"))
check(get("merged"))

copy("dont_merge", "testfile")
commit()

revert_to(anc)
copy("left", "testfile")
commit()
left = base_revision()

revert_to(anc)
copy("right", "testfile")
commit()
right = base_revision()

-- save the ancestry graph for later
check(mtn("automate", "graph"), 0, true, nil)
rename("stdout", "ancestry")

-- XXX If we don't do the revert_to(anc) here, we get the wrong
-- merge result.
revert_to(anc)
check(get("otherfile_mod"))
copy("otherfile_mod", "otherfile")

check(mtn("explicit_merge_and_update", left, right), 0, false, false)

-- testfile should be the same as merged
check(samefile("merged", "testfile"))

-- otherfile should still be the same as otherfile_mod
check(samefile("otherfile_mod", "otherfile"))

-- the database should be unaffected, i.e. the operation should not have
-- created any new revisions
check(mtn("automate", "graph"), 0, {"ancestry"}, nil)

-- both testfile and otherfile should be in state 'patched'
-- [test fails at this point because status has not been updated for
-- multi-parent workspaces]
check(mtn("status"), 0, "patched testfile\npatched otherfile\n", nil)

-- a commit at this point should succeed
commit()
merged = base_revision()

-- automate parents 'merged' should list both 'left' and 'right', in
-- no particular order, and nothing else
check(mtn("automate", "parents", merged), 0, true, nil)
want = { [left] = true, [right] = true }
L("parents of revision "..merged..":\n")
log_file_contents("stdout")
for line in io.open("stdout", "r"):lines() do
   if want[line] then
      want[line] = nil
   else
      err("Unexpected or duplicate parent: "..line.."\n", 3)
   end
end
