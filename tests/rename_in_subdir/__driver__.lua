
mtn_setup()

mkdir("subdir")
mkdir("subdir/anotherdir")
writefile("subdir/foo", "data data")
writefile("subdir/anotherdir/bar", "more data")
check(mtn("add", "-R", "."), 0, false, false)
commit()
 rev = base_revision()

check(indir("subdir", mtn("rename", "foo", "foo-renamed")), 0, false, false)
check(indir("subdir", mtn("rename", "anotherdir", "../anotherdir-renamed")), 0, false, false)
commit()

check(mtn("checkout", "--revision", rev, "codir"), 0, false, false)
check(indir("codir", mtn("update", "--branch=testbranch")), 0, false, false)
check(not exists("codir/subdir/foo"))
check(not exists("codir/subdir/anotherdir/bar"))
check(samefile("subdir/foo-renamed", "codir/subdir/foo-renamed"))
check(samefile("anotherdir-renamed/bar", "codir/anotherdir-renamed/bar"))
