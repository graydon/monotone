
mtn_setup()

mkdir("workspace")
check(indir("workspace", mtn("setup", "-b", "testbranch")), 0, false, false)

mkdir("workspace/dir1")
writefile("workspace/old_root_file", "I'm in the root to start off with!")
writefile("workspace/dir1/new_root_file", "I'm in the subdir to start off with.")
check(indir("workspace", mtn("add", "-R", ".")), 0, false, false)
check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)
base = indir("workspace", {base_revision})[1]()

check(indir("workspace", mtn("pivot_root", "dir1", "old_root")), 0, false, false)
check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)

check(mtn("co", "-r", base, "testspace"), 0, false, false)
writefile("new_old_root_file", "old root file modified")
writefile("new_new_root_file", "new root file modified")
writefile("new_unversioned_root_file", "newly placed in root dir, unversioned")
writefile("new_unversioned_subdir_file", "newly placed in sub dir, unversioned")
writefile("new_versioned_root_file", "newly placed in root dir, versioned")
writefile("new_versioned_subdir_file", "newly placed in sub dir, versioned")
check(copy("new_old_root_file", "testspace/old_root_file"))
check(copy("new_new_root_file", "testspace/dir1/new_root_file"))
check(copy("new_unversioned_root_file", "testspace"))
check(copy("new_unversioned_subdir_file", "testspace/dir1"))
check(copy("new_versioned_root_file", "testspace"))
check(copy("new_versioned_subdir_file", "testspace/dir1"))

check(indir("testspace", mtn("add", "new_versioned_root_file", "dir1/new_versioned_subdir_file")), 0, false, false)
check(indir("testspace", mtn("update")), 0, false, false)

check(samefile("new_old_root_file", "testspace/old_root/old_root_file"))
check(samefile("new_new_root_file", "testspace/new_root_file"))
check(samefile("new_unversioned_root_file", "testspace/old_root/new_unversioned_root_file"))
check(samefile("new_unversioned_subdir_file", "testspace/new_unversioned_subdir_file"))
check(samefile("new_versioned_root_file", "testspace/old_root/new_versioned_root_file"))
check(samefile("new_versioned_subdir_file", "testspace/new_versioned_subdir_file"))
