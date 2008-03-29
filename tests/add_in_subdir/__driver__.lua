
mtn_setup()

mkdir("subdir")
mkdir("subdir/anotherdir")
writefile("subdir/foo", "data data")
writefile("subdir/anotherdir/bar", "more data")

-- Add a file
chdir("subdir")
check(mtn("add", "foo"), 0, false, false)
-- Add a directory
check(mtn("add", "-R", "anotherdir"), 0, false, false)
chdir("..")

commit()
rev = base_revision()

check(mtn("checkout", "--revision", rev, "codir"), 0, false, false)
check(samefile("subdir/foo", "codir/subdir/foo"))
check(samefile("subdir/anotherdir/bar", "codir/subdir/anotherdir/bar"))
