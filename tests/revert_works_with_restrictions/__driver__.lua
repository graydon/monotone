
mtn_setup()

writefile("origfile", "some file")
writefile("orig.ignore", "a file type that is usually ignored")
writefile("orig2", "another file")
writefile("modified1", "this is different 1")
writefile("modified2", "this is different 2")
writefile("modified3", "this is different 3")

getfile("ignore_hook.lua")

copyfile("origfile", "testfile")
copyfile("orig.ignore", "file.ignore")
copyfile("orig2", "file2")
check(mtn("add", "testfile", "file.ignore", "file2"), 0, false, false)
commit()

-- modify the files, then revert the 'ignored' file
copyfile("modified1", "testfile")
copyfile("modified2", "file.ignore")

check(mtn("--rcfile=ignore_hook.lua", "revert", "file.ignore"), 0, false, false)

-- check that only the 'ignored' file was reverted
check(samefile("testfile", "modified1"))
check(samefile("file.ignore", "orig.ignore"))

-- now run it again with two paths, one in the ignorehook list, the other normal
check(mtn("revert", "."), 0, false, false)
copyfile("modified1", "testfile")
copyfile("modified2", "file.ignore")
copyfile("modified3", "file2")

check(mtn("--rcfile=ignore_hook.lua", "revert", "file.ignore", "testfile"), 0, false, false)

-- check that the files are correct
check(samefile("testfile", "origfile"))
check(samefile("file.ignore", "orig.ignore"))
check(samefile("file2", "modified3"))


-- now try just reverting missing files

copyfile("modified1", "testfile")
copyfile("modified2", "file.ignore")
remove("file2")

check(mtn("--rcfile=ignore_hook.lua", "revert", "--missing", ".", "--debug"), 0, false, false)

check(samefile("testfile", "modified1"))
check(samefile("file.ignore", "modified2"))
check(samefile("file2", "orig2"))


-- make sure that 'revert --missing' when there are no missing files doesn't
-- revert existing changes

copyfile("modified1", "testfile")
copyfile("orig.ignore", "file.ignore")
copyfile("orig2", "file2")

check(mtn("--rcfile=ignore_hook.lua", "revert", "--missing", ".", "--debug"), 0, false, false)

check(samefile("testfile", "modified1"))
check(samefile("file.ignore", "orig.ignore"))
check(samefile("file2", "orig2"))
