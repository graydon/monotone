-- this tests that the revision generated for an in-workspace merge
-- handles added files correctly.

mtn_setup()

check(get("testfile"))
copy("testfile", "testfile.b")
addfile("testfile")
commit()
anc = base_revision()

check(get("left"))
copy("left", "left.b")
addfile("left")
commit()
left = base_revision()

revert_to(anc)
check(remove("left"))
check(get("right"))
copy("right", "right.b")
addfile("right")
commit()
right = base_revision()

-- save the ancestry graph for later
check(mtn("automate", "graph"), 0, true, nil)
rename("stdout", "ancestry")

-- the merge should succeed
check(mtn("merge_into_workspace", left), 0, false, false)

-- testfile, left, right should all exist and equal their originals

check(samefile("testfile", "testfile.b"))
check(samefile("left", "left.b"))
check(samefile("right", "right.b"))

-- the database should be unaffected, i.e. the operation
-- should not have created any new revisions
check(mtn("automate", "graph"), 0, {"ancestry"}, nil)

-- "automate get_revision" should report each of "left" and "right" as
-- added versus their respective parents, and no other changes
check(get("expected-revision"))
check(mtn("automate", "get_current_revision"), 0, {"expected-revision"}, nil)

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
