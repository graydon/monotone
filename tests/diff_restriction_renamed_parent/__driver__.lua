mtn_setup()

mkdir("parent1")
addfile("parent1/file1", "something")
commit()
orig = base_revision()

-- rename the parent 
check(mtn("rename", "parent1", "parent2"), 0, true, true)
commit()

-- alter the file
writefile("parent2/file1", "something else")
commit()

xfail(mtn("diff", "-r", orig, "parent2/file1"), 0, true, true)
