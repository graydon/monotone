
mtn_setup()

revs = {}

mkdir("groundzero")
addfile("groundzero/preexisting", "1")
addfile("groundzero/rename-out", "2")
addfile("rename-in", "3")
addfile("groundzero/double-kill", "4")
addfile("bystander1", "5")
commit()
revs.base = base_revision()

addfile("bystander2", "6")
addfile("groundzero/new-file", "7")
rename("rename-in", "groundzero/rename-in")
check(mtn("rename", "--bookkeep-only", "rename-in", "groundzero/rename-in"), 0, false, false)
rename("groundzero/rename-out", "rename-out")
check(mtn("rename", "--bookkeep-only", "groundzero/rename-out", "rename-out"), 0, false, false)
check(mtn("drop", "groundzero/double-kill"), 0, false, false)
commit()
revs.other = base_revision()

remove("groundzero")
revert_to(revs.base)
remove("bystander2")
remove("rename-out")

-- update doesn't remove files...
remove("groundzero/rename-in")
remove("groundzero/new-file")

check(mtn("drop", "groundzero"), 1, false, false)
check(mtn("drop", "--recursive", "groundzero"), 0, false, false)
commit()
revs.dir_del = base_revision()


-- orphaned node conflicts on rename-in and new-file
check(mtn("merge"), 1, false, false)

check(mtn("update", "-r", revs.other), 0, false, false)
check(mtn("drop", "--bookkeep-only", "groundzero/new-file"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "groundzero/rename-in"), 0, false, false)
commit()
check(mtn("merge"), 0, false, false)


check(mtn("checkout", "--revision", revs.base, "clean"), 0, false, false)
chdir("clean")
check(mtn("--branch=testbranch", "update"), 0, false, false)
chdir("..")

check(not exists("clean/rename-out"))
check(exists("clean/bystander1"))
check(exists("clean/bystander2"))
check(not exists("clean/groundzero/rename-in"))
check(not exists("clean/groundzero/preexisting"))
check(not exists("clean/groundzero/double-kill"))
check(not exists("clean/groundzero/new-file"))
-- Just in case:
check(not exists("clean/rename-in"))
check(not exists("clean/groundzero/rename-out"))
