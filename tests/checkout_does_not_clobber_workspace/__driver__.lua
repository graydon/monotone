mtn_setup()

mkdir("foo")
addfile("file1", "file1")
addfile("foo/file2", "foofile2")
commit()

-- checkout to clean workspace
mkdir("test1")
check(indir("test1", mtn("checkout", ".")))

-- checkout to workspace with an unversioned file blocking a versioned file
mkdir("test2")
writefile("test2/file1", "blocker")
check(indir("test2", mtn("checkout", ".")), 1, false, true)
check(not samefile("file1", "test2/file1"))

-- checkout to workspace with an unversioned directory blocking a versioned directory
mkdir("test3")
mkdir("test3/foo")
writefile("test3/foo/asdf", "asdf")
check(indir("test3", mtn("checkout", ".")), 1, false, true)
check(exists("test3/foo/asdf"))

-- checkout to workspace with an unversioned directory blocking a versioned file
mkdir("test4")
mkdir("test4/file1")
check(indir("test4", mtn("checkout", ".")), 1, false, true)

-- checkout to workspace with an unversioned file blocking a versioned directory
mkdir("test5")
writefile("test5/foo", "foobar")
check(indir("test5", mtn("checkout", ".")), 1, false, true)
