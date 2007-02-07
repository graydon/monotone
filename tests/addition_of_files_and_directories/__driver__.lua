
mtn_setup()

mkdir("dir")
writefile("file0", "file 0\n")
writefile("dir/file1", "file 1\n")
writefile("dir/file2", "file 2\n")

-- adding a non-existent file should fail

check(mtn("add", "foobar"), 1, false, false)

-- newly added files should appear as such

check(mtn("add", "file0"), 0, false, true)
check(qgrep("adding file0", "stderr"))

check(mtn("add", "dir"), 0, false, true)
check(not qgrep("adding dir/file1", "stderr"))
check(not qgrep("adding dir/file2", "stderr"))

check(mtn("add", "-R", "dir"), 0, false, true)
check(qgrep("adding dir/file1", "stderr"))
check(qgrep("adding dir/file2", "stderr"))

check(mtn("status"), 0, true)
check(qgrep("file0", "stdout"))
check(qgrep("file1", "stdout"))
check(qgrep("file2", "stdout"))

commit()

-- redundant additions should not appear 
-- (i.e. they should be ignored)

check(mtn("add", "file0"), 0, false, true)
check(qgrep("skipping file0", "stderr"))

check(mtn("add", "-R", "dir"), 0, false, true)
check(qgrep("skipping dir/file1", "stderr"))
check(qgrep("skipping dir/file2", "stderr"))

check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))

-- add --unknown should add any files that ls unknown shows you and not ignored

writefile("file3", "file 3\n")
--writefile("file4.ignore", "file 4 ignore\n")
writefile("dir/file5", "file 5\n")
writefile("dir/file6.ignore", "file 6\n")
mkdir("dir2")
writefile("dir2/file7", "file 7\n")
--writefile(".mtn-ignore", ".*\\.ignore$\n")

--check(raw_mtn("ls", "unkown"), 0, true, false)

check(mtn("add", "--unknown"), 0, false, true)
check(qgrep('adding file3', "stderr"))
--check(not qgrep('adding file4.ignore', "stderr"))
check(qgrep('adding dir/file5', "stderr"))
--check(not qgrep('adding dir/file6.ignore', "stderr"))
check(qgrep('adding dir2', "stderr"))
check(not qgrep('adding dir2/file7', "stderr"))
check(not qgrep('skipping dir2/file7', "stderr"))
check(not qgrep('adding test_hooks.lua', "stderr"))

check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))
check(qgrep("file3", "stdout"))
--check(not qgrep("file4", "stdout"))
check(qgrep("file5", "stdout"))
--check(not qgrep("file6", "stdout"))

commit()

check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))
check(not qgrep("file3", "stdout"))
--check(not qgrep("file4", "stdout"))
check(not qgrep("file5", "stdout"))
--check(not qgrep("file6", "stdout"))
