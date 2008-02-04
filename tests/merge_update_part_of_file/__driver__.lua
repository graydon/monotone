--
-- Checks updating of a file which already has part of the changes that
-- are in head.  That already-committed chunk can confuse the merge
-- algorithm because of the similarities of the removed text from the
-- text that was left.
--
-- This test goes like this:
-- 1. Commits a file that has two similar parts (conditionals).
-- 2. Removes one of the conditionals and commits it.
-- 3. Reverts to the previous revision.
-- 4. Applies the change done in 2 and another change in another part of
--    the file (which results in a diff with two chunks).
-- 5. Updates the workspace to the latest revision.
-- 6. A diff of the test file should only report the second chunk as
--    modified.
--

mtn_setup()

check(get("original"))
check(get("onechange"))
check(get("twochanges"))
check(get("before.diff"))
check(get("after.diff"))

copy("original", "testfile")
addfile("testfile")
commit()
anc = base_revision()

copy("onechange", "testfile")
commit()

revert_to(anc)
copy("twochanges", "testfile")
check(mtn("diff", "testfile"), 0, true)
canonicalize("stdout")
rename("stdout", "monodiff")
check(samefile("monodiff", "before.diff"))

check(mtn("update"), 0, false, true)

check(mtn("diff", "testfile"), 0, true)
canonicalize("stdout")
rename("stdout", "monodiff")
check(samefile("monodiff", "after.diff"))
