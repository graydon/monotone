
--
-- Check if a dropped node is properly reverted to its new path
-- which is determined by a not reverted rename of an ancestor
-- (old_dir -> new_dir)
--

mtn_setup()

check(mtn("mkdir", "old_dir"), 0, false, false)
writefile("old_dir/a", "blabla")
addfile("old_dir/a")
commit()

check(mtn("mv", "old_dir", "new_dir"), 0, false, false)
check(mtn("drop", "new_dir/a"), 0, false, false)

--
-- this should re-create a under new_dir/, but not under the
-- old path old_dir/a, i.e. mtn status should not return nonzero
-- because it found missing files
--
check(mtn("revert", "old_dir/a"), 0, false, false)
check(mtn("status"), 0, false, false)
