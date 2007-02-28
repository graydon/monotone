
mtn_setup()

-- "rename" needs to check that it isn't overwriting existing
-- files/directories.

addfile("target_file", "blah blah")
mkdir("target_dir")
addfile("target_dir/subfile", "stuff stuff")

addfile("rename_file", "foo foo")
mkdir("rename_dir")
addfile("rename_dir/file", "bar bar")

check(mtn("rename", "--bookkeep-only", "unknown_file", "other_file"), 1, false, false)
check(mtn("rename", "--bookkeep-only", "rename_file", "target_file"), 1, false, false)
check(mtn("rename", "--bookkeep-only", "rename_dir", "target_file"), 1, false, false)

commit()

check(mtn("rename", "--bookkeep-only", "unknown_file", "other_file"), 1, false, false)
check(mtn("rename", "--bookkeep-only", "rename_file", "target_file"), 1, false, false)
check(mtn("rename", "--bookkeep-only", "rename_dir", "target_file"), 1, false, false)
