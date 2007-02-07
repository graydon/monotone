
mtn_setup()

mkdir("subdir")
mkdir("subdir/anotherdir")
writefile("subdir/foo", "data data")
writefile("subdir/anotherdir/bar", "more data")
check(mtn("add", "-R", "."), 0, false, false)
commit()
rev = base_revision()

-- Create a checkout we can play with
check(mtn("checkout", "--revision", rev, "codir"), 0, false, false)

-- Write to the checked out files
writefile("codir/subdir/foo", "other data")
writefile("codir/subdir/anotherdir/bar", "more other data")

-- Revert them
chdir("codir/subdir")
check(mtn("revert", "foo"), 0, false, false)
check(mtn("revert", "anotherdir"), 0, false, false)
chdir("../..")

-- Check them
check(samefile("subdir/foo", "codir/subdir/foo"))
check(samefile("subdir/anotherdir/bar", "codir/subdir/anotherdir/bar"))
