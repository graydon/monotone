
mtn_setup()

-- this test is a bug report
-- the situation where a file is renamed to a dir should be trapped and 
-- reported with N(...) or something

addfile("file", "file")
commit()

mkdir("dir")
xfail_if(true, mtn("rename", "file", "dir"), 1, false, false)
check(mtn("status"), 0, false, false)
check(mtn("diff"), 0, false, false)
