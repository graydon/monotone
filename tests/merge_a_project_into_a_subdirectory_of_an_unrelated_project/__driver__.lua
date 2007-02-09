
mtn_setup()

copy("_MTN", "_MTN.orig")
addfile("file1", "foo")
commit("b1")

remove("_MTN")
copy("_MTN.orig", "_MTN")
remove("file1")
addfile("file2", "bar")
mkdir("dir")
addfile("dir/quux", "baz")
commit("b2")

check(mtn("merge_into_dir", "b1", "b2", "dir/zuul"), 0, false, false)

check(mtn("checkout", "-b", "b2", "checkout"), 0, false, true)
check(exists("checkout/file2"))
check(isdir("checkout/dir"))
check(exists("checkout/dir/quux"))
check(isdir("checkout/dir/zuul"))
check(exists("checkout/dir/zuul/file1"))
