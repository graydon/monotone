
mtn_setup()

mkdir("subdir")
mkdir("subdir/anotherdir")
writefile("subdir/foo", "data data")
writefile("subdir/anotherdir/bar", "more data")
check(mtn("add", "-R", "."), 0, false, false)
commit()
rev = base_revision()

-- Create a checkout we can update
check(mtn("checkout", "--revision", rev, "codir"), 0, false, false)

chdir("subdir")
check(mtn("drop", "--bookkeep-only", "foo"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "--recursive", "anotherdir"), 0, false, false)
chdir("..")
commit()

chdir("codir")
check(mtn("update"), 0, false, false)
chdir("..")
check(not exists("codir/subdir/foo"))
check(not exists("codir/subdir/anotherdir/bar"))
