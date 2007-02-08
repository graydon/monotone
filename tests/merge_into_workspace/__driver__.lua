-- this is very similar to the explicit_merge test, except that the
-- result of the merge winds up in the current workspace.

mtn_setup()

check(get("testfile"))
check(get("otherfile"))
check(get("dont_merge"))
check(get("left"))
check(get("right"))
check(get("merged"))
check(get("otherfile_mod"))

addfile("testfile")
addfile("otherfile")
commit()
anc = base_revision()

copy("dont_merge", "testfile")
commit()
unrelated = base_revision()

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

-- we're currently at "right", so a merge_into_workspace of "left"
-- is the merge we want; but first, test the sanity checks

copy("otherfile_mod", "otherfile")

-- this should fail, because the workspace is unclean
check(mtn("merge_into_workspace", left), 1, false, false)

-- after reverting, it should work
check(mtn("revert", "otherfile"), 0, false, false)
check(mtn("merge_into_workspace", left), 0, false, false)

-- testfile should be the same as merged
check(samefile("merged", "testfile"))

-- the database should be unaffected, i.e. the operation
-- should not have created any new revisions
check(mtn("automate", "graph"), 0, {"ancestry"}, nil)

-- testfile should be in state 'patched'
check(mtn("status"), 0, true, nil)
check(qgrep("patched testfile", "stdout"))

-- some automate commands that should do sensible things
check(mtn("automate", "get_current_revision_id"), 0,
      "9491240daab3028f583e4d0e0239da3e626fda2b\n", nil)

check(get("expected-manifest"))
check(mtn("automate", "get_manifest_of"), 0, {"expected-manifest"}, nil)

-- a commit at this point should succeed
commit()
merged = base_revision()

-- automate parents 'merged' should list both 'left' and
-- 'right', in no particular order, and nothing else
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
