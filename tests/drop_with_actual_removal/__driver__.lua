
mtn_setup()

addfile("file0", "normal committed")
addfile("file1", "normal committed bookkeep-only")
addfile("file2", "changed committed")
addfile("file3", "changed committed bookkeep-only")
writefile("file4", "untracked")
writefile("file5", "untracked bookkeep-only")

check(mtn("mkdir", "testdir1"), 0, false, false)
check(mtn("mkdir", "testdir2"), 0, false, false)
mkdir("testdir3")
addfile("testdir3/dirfile", "dirfile")
mkdir("testdir4")
addfile("testdir4/dirfile", "dirfile")
check(mtn("mkdir", "testdir5"), 0, false, false)
check(mtn("mkdir", "testdir6"), 0, false, false)

check(mtn("mkdir", "testdir13"), 0, false, false)
check(mtn("mkdir", "testdir14"), 0, false, false)
mkdir("testdir15")
addfile("testdir15/dirfile", "dirfile")
mkdir("testdir16")
addfile("testdir16/dirfile", "dirfile")
check(mtn("mkdir", "testdir17"), 0, false, false)
check(mtn("mkdir", "testdir18"), 0, false, false)

commit()

writefile("file2", "a change")
writefile("file3", "a change")
writefile("file4", "untracked file")
writefile("file4", "untracked file bookkeep only")

addfile("file6", "added, uncommitted")
addfile("file7", "added, uncommitted bookkeep-only")

writefile("testdir5/dirfile", "committed dir, uncommitted file")
writefile("testdir6/dirfile", "committed dir, uncommitted file bookkeep only")
writefile("testdir17/dirfile", "committed dir, uncommitted file -R")
writefile("testdir18/dirfile", "committed dir, uncommitted file bookkeep only -R")

mkdir("testdir7")
mkdir("testdir8")

check(mtn("mkdir", "testdir9"), 0, false, false)
check(mtn("mkdir", "testdir10"), 0, false, false)
mkdir("testdir11")
addfile("testdir11/dirfile", "dirfile")
mkdir("testdir12")
addfile("testdir12/dirfile", "dirfile")

mkdir("testdir19")
mkdir("testdir20")

check(mtn("mkdir", "testdir21"), 0, false, false)
check(mtn("mkdir", "testdir22"), 0, false, false)
mkdir("testdir23")
addfile("testdir23/dirfile", "dirfile")
mkdir("testdir24")
addfile("testdir24/dirfile", "dirfile")


-- begin tests here

-- drop of normal committed file
-- it is removed
check(mtn("drop", "file0"), 0, false, false)
check(not exists("file0"))

-- drop of normal committed file --bookkeep-only
-- it remains
check(mtn("drop", "--bookkeep-only", "file1"), 0, false, false)
check(exists("file1"))

-- drop of changed committed file
-- it remains and a warning is printed
check(mtn("drop", "file2"), 0, false, true)
check(qgrep("file file2 changed", "stderr"))
check(exists("file2"))

-- drop of changed committed file --bookkeep-only
-- it remains
check(mtn("drop", "--bookkeep-only", "file3"), 0, false, false)
check(exists("file3"))

-- drop of untracked file
-- it remains
check(mtn("drop", "file4"), 0, false, false)
check(exists("file4"))

-- drop of untracked file --bookkeep-only
-- it remains
check(mtn("drop", "--bookkeep-only", "file5"), 0, false, false)
check(exists("file5"))

-- drop of added but uncommitted file
-- it remains
check(mtn("drop", "file6"), 0, false, false)
check(exists("file6"))

-- drop of added but uncommitted file --bookkeep-only
-- it remains
check(mtn("drop", "--bookkeep-only", "file7"), 0, false, false)
check(exists("file7"))

-- drop of empty committed dir
-- removed
check(mtn("drop", "testdir1"), 0, false, false)
check(not exists("testdir1"))

-- drop of empty committed dir --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir2"), 0, false, false)
check(exists("testdir2"))

-- drop of committed dir with committed file
-- not removed
check(mtn("drop", "testdir3"), 1, false, false)
check(exists("testdir3/dirfile"))

-- drop of committed dir with committed file --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir4"), 1, false, false)
check(exists("testdir4/dirfile"))

-- drop of committed dir with uncommitted file
-- not removed
check(mtn("drop", "testdir5"), 0, false, false)
check(exists("testdir5/dirfile"))

-- drop of committed dir with uncommitted file --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir6"), 0, false, false)
check(exists("testdir6/dirfile"))

-- drop of untracked dir
-- not removed
check(mtn("drop", "testdir7"), 0, false, false)
check(exists("testdir7"))

-- drop of untracked dir --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir8"), 0, false, false)
check(exists("testdir8"))

-- drop of added empty dir
-- not removed
check(mtn("drop", "testdir9"), 0, false, false)
check(exists("testdir9"))

-- drop of added empty dir --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir10"), 0, false, false)
check(exists("testdir10"))

-- drop of added dir with added file
-- not removed
check(mtn("drop", "testdir11"), 1, false, false)
check(exists("testdir11/dirfile"))

-- drop of added dir with added file --bookkeep-only
-- not removed
check(mtn("drop", "--bookkeep-only", "testdir12"), 1, false, false)
check(exists("testdir12/dirfile"))

-- drop of empty committed dir -R
-- removed
check(mtn("drop", "-R", "testdir13"), 0, false, false)
check(not exists("testdir13"))

-- drop of empty committed dir --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir14"), 0, false, false)
check(exists("testdir14"))

-- drop of committed dir with committed file -R
-- removed
check(mtn("drop", "-R", "testdir15"), 0, false, false)
check(not exists("testdir15"))

-- drop of committed dir with committed file --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir16"), 0, false, false)
check(exists("testdir16/dirfile"))

-- drop of committed dir with uncommitted file -R
-- not removed
check(mtn("drop", "-R", "testdir17"), 0, false, false)
check(exists("testdir17/dirfile"))

-- drop of committed dir with uncommitted file --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir18"), 0, false, false)
check(exists("testdir18/dirfile"))

-- drop of untracked dir -R
-- not removed
check(mtn("drop", "-R", "testdir19"), 0, false, false)
check(exists("testdir19"))

-- drop of untracked dir --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir20"), 0, false, false)
check(exists("testdir20"))

-- drop of added dir -R
-- not removed
check(mtn("drop", "-R", "testdir21"), 0, false, false)
check(exists("testdir21"))

-- drop of added dir --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir22"), 0, false, false)
check(exists("testdir22"))

-- drop of added dir with added file -R
-- not removed
check(mtn("drop", "-R", "testdir23"), 0, false, false)
check(exists("testdir23/dirfile"))

-- drop of added dir with added file --bookkeep-only -R
-- not removed
check(mtn("drop", "-R", "--bookkeep-only", "testdir24"), 0, false, false)
check(exists("testdir24/dirfile"))
