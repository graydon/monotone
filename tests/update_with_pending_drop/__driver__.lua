
mtn_setup()

addfile("file", "test1")

commit()

check(mtn("--branch=testbranch", "co", "codir"), 0, false, false)

remove("file")
check(mtn("drop", "--bookkeep-only", "file"), 0, false, false)

commit()

rev = base_revision()

check(indir("codir", mtn("drop", "--bookkeep-only", "file")), 0, false, false)
check(indir("codir", mtn("update")), 0, false, false)

check(indir("codir", mtn("automate", "get_revision", rev)), 0, false, false)

-- make sure there are no changes in the workspace

check(indir("codir", mtn("diff")), 0, true, false)
check(grep('no changes', "stdout"), 0, false, false)
