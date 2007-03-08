mtn_setup()

-- pivot_root to a directory that hasn't been commited yet should work

mkdir("workspace")
check(indir("workspace", mtn("setup", ".", "-b", "testbranch")),
      0, false, false)

writefile("workspace/file1", "blah blah")
check(indir("workspace", mtn("add", ".")), 0, false, false)

check(indir("workspace", mtn("commit", "-m", "foo")), 0, false, false)

mkdir("workspace/new_root")
check(indir("workspace", mtn("add", "new_root")), 0, false, false)

check(indir("workspace", mtn("pivot_root", "new_root", "Attic")),
      0, false, false)

check(isdir("workspace/_MTN"))
check(isdir("workspace/Attic"))
check(exists("workspace/Attic/file1"))
check(not exists("workspace/new_root"))

check(indir("workspace", mtn("commit", "-m", "pivotted")), 0, false, false)
