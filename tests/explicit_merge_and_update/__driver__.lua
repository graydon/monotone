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

check(get("otherfile_mod"))
for d,r in pairs({ anc_d = anc,
		   unrelated_d = unrelated,
		   left_d = left,
		   right_d = right }) do
   check(mtn("checkout", "-r"..r, d), 0, false, false)
   copy("otherfile_mod", d.."/otherfile")

   check(indir(d, mtn("explicit_merge_and_update", left, right)), 0, false, false)

   -- testfile should be the same as merged
   check(samefile("merged", d.."/testfile"))

   -- otherfile should still be the same as otherfile_mod
   check(samefile("otherfile_mod", d.."/otherfile"))

   -- the database should be unaffected, i.e. the operation
   -- should not have created any new revisions
   check(indir(d, mtn("automate", "graph")), 0, {"ancestry"}, nil)

   -- both testfile and otherfile should be in state 'patched'
   check(indir(d, mtn("status")), 0, true, nil)
   check(qgrep("patched testfile", "stdout"))
   check(qgrep("patched otherfile", "stdout"))

   -- a commit at this point should succeed
   check(indir(d, commit))
   merged = indir(d, base_revision)

   -- automate parents 'merged' should list both 'left' and
   -- 'right', in no particular order, and nothing else
   check(indir(d, mtn("automate", "parents", merged)), 0, true, nil)
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
end
check(false)
