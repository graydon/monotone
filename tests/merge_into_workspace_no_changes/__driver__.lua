-- this tests that we get a correct (and committable) merge in the
-- workspace even when it has no changes to commit.

mtn_setup()

check(get("testfile"))
check(get("left"))
check(get("right"))
check(get("unrelated"))

addfile("testfile")
commit()
anc = base_revision()

copy("unrelated", "testfile")
commit()
unrelated = base_revision()

revert_to(anc)
copy("left", "testfile")
commit()
left = base_revision()

revert_to(anc)
copy("right", "testfile")
commit()

copy("left", "testfile")
commit()
right = base_revision()

-- save the ancestry graph for later
check(mtn("automate", "graph"), 0, true, nil)
rename("stdout", "ancestry")

-- the merge should succeed
check(mtn("merge_into_workspace", left), 0, false, false)

-- testfile should be the same as left
check(samefile("left", "testfile"))

-- the database should be unaffected, i.e. the operation
-- should not have created any new revisions
check(mtn("automate", "graph"), 0, {"ancestry"}, nil)

-- status should report no changes
check(mtn("status"), 0, true, nil)
check(qgrep("no changes", "stdout"))

-- we should not be able to merge anything on top of this, despite
-- there being no changes in the workspace
check(mtn("merge_into_workspace", unrelated), 1, false, false)

-- a commit at this point should succeed, again despite having no changes
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
