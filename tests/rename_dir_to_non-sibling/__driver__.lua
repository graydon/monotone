
mtn_setup()

-- Pretty rigorous test of rename_dir, in particular when the target
-- and source of the rename are not sibling directories.

old_dir = "subdir1/the_dir"
new_dir = "subdir2/target_dir/the_dir"

mkdir("subdir1")
mkdir(old_dir)
mkdir("subdir2")
mkdir("subdir2/target_dir")

addfile(old_dir .. "/preexisting", "foo bar blah")
addfile(old_dir .. "/rename-out-file", "asdfasdf")
mkdir(old_dir .. "/rename-out-dir")
addfile(old_dir .. "/rename-out-dir/subfile", "9")
addfile("rename-in-file", "badlsakl")
mkdir("rename-in-dir")
addfile("rename-in-dir/subfile", "10")
addfile(old_dir .. "/doomed", "badfsda")
addfile("subdir1/bystander1", "stuff stuff")
commit()
base = base_revision()

addfile("subdir1/bystander2", "asdfasknb")
addfile(old_dir .. "/new-file", "foo ping")
check(mtn("rename", "--bookkeep-only", old_dir .. "/rename-out-file", "rename-out-file"), 0, false, false)
rename(old_dir .. "/rename-out-file", "rename-out-file")
check(mtn("rename", "--bookkeep-only", old_dir .. "/rename-out-dir", "rename-out-dir"), 0, false, false)
rename(old_dir .. "/rename-out-dir", "rename-out-dir")
check(mtn("rename", "--bookkeep-only", "rename-in-dir", old_dir .. "/rename-in-dir"), 0, false, false)
rename("rename-in-dir", old_dir .. "/rename-in-dir")
check(mtn("rename", "--bookkeep-only", "rename-in-file", old_dir .. "/rename-in-file"), 0, false, false)
rename("rename-in-file", old_dir .. "/rename-in-file")
check(mtn("drop", "--bookkeep-only", old_dir .. "/doomed"), 0, false, false)
commit()
left = base_revision()

rename("subdir1", "backup")

revert_to(base)

check(mtn("rename", "--bookkeep-only", old_dir, new_dir), 0, false, false)
rename(old_dir, new_dir)
commit()
right = base_revision()

check(mtn("merge", "--branch=testbranch"), 0, false, false)

check(mtn("checkout", "--revision", base, "test_dir"), 0, false, true)
check(indir("test_dir", mtn("--branch=testbranch", "update")), 0, false, false)
merged = indir("test_dir", {base_revision})[1]()
check(base ~= merged)
check(left ~= merged)
check(right ~= merged)

t_new_dir = "test_dir/" .. new_dir
check(samefile(new_dir .. "/preexisting", t_new_dir .. "/preexisting"))
check(samefile("backup/the_dir/new-file", t_new_dir .. "/new-file"))
check(samefile(new_dir .. "/rename-out-file", "test_dir/rename-out-file"))
check(not exists(t_new_dir .. "/rename-out-file"))
check(samefile(new_dir .. "/rename-out-dir/subfile", "test_dir/rename-out-dir/subfile"))
check(not exists(t_new_dir .. "/rename-out-dir/subfile"))
check(samefile("backup/the_dir/rename-in-file", t_new_dir .. "/rename-in-file"))
check(not exists("test_dir/rename-in-file"))
check(samefile("backup/the_dir/rename-in-dir/subfile", t_new_dir .. "/rename-in-dir/subfile"))
check(not exists("test_dir/rename-in-dir/subfile"))
check(not exists(t_new_dir .. "/doomed"))
check(samefile("subdir1/bystander1", "test_dir/subdir1/bystander1"))
check(samefile("backup/bystander2", "test_dir/subdir1/bystander2"))
