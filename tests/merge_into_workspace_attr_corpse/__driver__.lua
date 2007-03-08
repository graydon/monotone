-- this tests that the revision generated for an in-workspace merge
-- handles attr corpses (see roster.cc) correctly.

mtn_setup()

check(get("testfile"))
copy("testfile", "testfile.b")
addfile("testfile")
commit()
anc = base_revision()

-- on each side, add and then delete a different attribute.
check(mtn("attr", "set", "testfile", "left-attr", "moose"))
commit()
check(mtn("attr", "drop", "testfile", "left-attr"))
commit()
left = base_revision()

revert_to(anc)
check(mtn("attr", "set", "testfile", "right-attr", "squirrel"))
commit()
check(mtn("attr", "drop", "testfile", "right-attr"))
commit()
right = base_revision()

-- save the ancestry graph for later
check(mtn("automate", "graph"), 0, true, nil)
rename("stdout", "ancestry")

-- the merge should succeed
check(mtn("merge_into_workspace", left), 0, false, false)

-- the database should be unaffected, i.e. the operation
-- should not have created any new revisions
check(mtn("automate", "graph"), 0, {"ancestry"}, nil)

-- testfile should exist and equal its original

check(samefile("testfile", "testfile.b"))

-- the resultant roster should look just like the roster that an
-- in-database merge would generate; in particular, both "left-attr"
-- and "right-attr" should show up as "dormant_attr"s.
check(get("expected-roster"))
check(mtn("get_roster"), 0, true, nil)
canonicalize("stdout")
check(samefile("expected-roster", "stdout"))

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
