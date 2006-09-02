
mtn_setup()

addfile("testfile1", "foo")
addfile("testfile2", "bar")
commit()

remove("testfile1")
remove("testfile2")

-- status should successfully report on the status of things regardless
-- of the status of those things -- i.e. it should report missing files
-- as such rather than failing on them.

-- status should list all missing files before failing 
-- if/when there are missing files

check(mtn("status"), 1, false, true)
check(qgrep("testfile1", "stderr"))
check(qgrep("testfile2", "stderr"))
