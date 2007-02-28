
mtn_setup()

writefile("original", "some stuff here")

check(mtn("add", "original"), 0, false, false)
commit()
base = base_revision()

-- drop it
check(mtn("drop", "--bookkeep-only", "original"), 0, false, false)
commit()

revert_to(base)

-- patch and rename it
rename("original", "different")
check(mtn("rename", "--bookkeep-only", "original", "different"), 0, false, false)
append("different", "more\n")
commit()

check(mtn("merge"), 0, false, false)
check(mtn("checkout", "-b", "testbranch", "clean"), 0, false, false)

-- check that the file doesn't exist
check(not exists("clean/original"))
check(not exists("clean/different"))
