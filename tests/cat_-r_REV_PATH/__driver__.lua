
mtn_setup()
revs = {}

writefile("r0testfile", "r0 test file")
writefile("r0otherfile", "r0 other file")
writefile("r1testfile", "r1 test file")
writefile("subfile", "data in subfile")

copy("r0testfile", "testfile")
copy("r0otherfile", "otherfile")
mkdir("subdir")
copy("subfile", "subdir/testfile")
check(mtn("add", "testfile", "otherfile", "subdir/testfile"), 0, false, false)
commit()
revs[0] = base_revision()

copy("r1testfile", "testfile")
commit()
revs[1] = base_revision()

check(mtn("cat", "-r", revs[0], "testfile"), 0, true, false)
canonicalize("stdout")
check(samefile("stdout", "r0testfile"))

check(mtn("cat", "-r", revs[0], "otherfile"), 0, true, false)
canonicalize("stdout")
check(samefile("stdout", "r0otherfile"))

check(mtn("cat", "-r", revs[1], "testfile"), 0, true, false)
canonicalize("stdout")
check(samefile("stdout", "r1testfile"))

check(indir("subdir", mtn("cat", "-r", revs[0], "testfile")), 0, true, false)
check(samefile("stdout", "subfile"))

remove("_MTN")

check(mtn("cat", "-r", revs[0], "testfile"), 0, true, false)
check(samefile("stdout", "r0testfile"))

check(mtn("cat", "-r", revs[0], "no_such_file"), 1, false, false)
check(mtn("cat", "-r", revs[0], ""), 1, false, false)
