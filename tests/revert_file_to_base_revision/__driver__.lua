
mtn_setup()

addfile("testfile0", "version 0 of first test file\n")
v1 = sha1("testfile0")
commit()

-- check reverting a single file by name

writefile("testfile0", "squirrils monkeys dingos\n")

check(qgrep("squirrils", "testfile0"))
check(mtn("revert", "testfile0"), 0, false, false)
check(not qgrep("squirrils", "testfile0"))
v2 = sha1("testfile0")
check(v1 == v2)


-- check reverting the whole tree

writefile("testfile0", "squirrils monkeys dingos\n")
check(mtn("status"), 0, true)
check(qgrep("testfile0", "stdout"))
check(mtn("revert", "."), 0, false, false)
check(not qgrep("testfile0", "_MTN/revision"))
check(mtn("status"), 0, true)
check(not qgrep("testfile0", "stdout"))


-- check reverting a delete

check(mtn("drop", "--bookkeep-only", "testfile0"), 0, false, false)
check(qgrep("testfile0", "_MTN/revision"))
check(mtn("status"), 0, true)
check(qgrep("testfile0", "stdout"))
check(mtn("revert", "."), 0, false, false)
check(not qgrep("testfile0", "_MTN/revision"))
check(mtn("status"), 0, true)
check(not qgrep("testfile0", "stdout"))


-- check reverting a change and a delete

f = io.open("testfile0", "a")
f:write("liver and maude\n")
f:close()
check(mtn("drop", "--bookkeep-only", "testfile0"), 0, false, false)
check(qgrep("testfile0", "_MTN/revision"))
check(mtn("status"), 0, true)
check(qgrep("testfile0", "stdout"))
check(mtn("revert", "testfile0"), 0, false, false)
check(not qgrep("testfile0", "_MTN/revision"))
check(mtn("status"), 0, true)
check(not qgrep("testfile0", "stdout"))
v3 = sha1("testfile0")
check(v1 == v3)

-- check reverting an add

addfile("testfile1", "squirrils monkeys dingos\n")
check(qgrep("testfile1", "_MTN/revision"))
check(mtn("status"), 0, true)
check(qgrep("testfile1", "stdout"))
check(mtn("revert", "."), 0, false, false)
check(not qgrep("testfile1", "_MTN/revision"))
check(mtn("status"), 0, true)
check(not qgrep("testfile1", "stdout"))

-- check reverting a directory

mkdir("sub")
addfile("sub/testfile2", "maude\n")
check(mtn("commit", "--message=new file"), 0, false, false)
writefile("sub/testfile2", "liver\n")
check(mtn("status"), 0, true)
check(qgrep("sub", "stdout"))
check(mtn("revert", "sub"), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("sub", "stdout"))

-- it also shouldn't matter how we spell the subdirectory name
writefile("sub/testfile2", "liver\n")
check(mtn("status"), 0, true)
check(qgrep("sub", "stdout"))
check(mtn("revert", "sub/"), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("sub", "stdout"))

-- check reverting a missing file
check(mtn("revert", "."), 0, false, false)
remove("testfile0")
check(mtn("status"), 1, false, false)
check(mtn("revert", "testfile0"), 0, true, false)
check(mtn("status"), 0, false, false)

-- check reverting some changes and leaving others

check(mtn("revert", "."), 0, false, false)
check(mtn("status"), 0, true)

copy("testfile0", "foofile0")
copy("sub/testfile2", "sub/foofile2")

check(mtn("rename", "--bookkeep-only", "testfile0", "foofile0"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "sub/testfile2", "sub/foofile2"), 0, false, false)

check(mtn("status"), 0, true)
check(qgrep("foofile0", "stdout"))
check(qgrep("foofile2", "stdout"))

check(mtn("revert", "sub/foofile2"), 0, true)
check(mtn("status"), 0, true)
check(qgrep("foofile0", "stdout"))
check(not qgrep("foofile2", "stdout"))

check(mtn("revert", "foofile0"), 0, true)
check(mtn("status"), 0, true)
check(not qgrep("foofile0", "stdout"))
check(not qgrep("foofile2", "stdout"))

-- check that "revert" by itself just prints an error
writefile("foofile0", "blah\n")
v1 = sha1("foofile0")
check(mtn("revert"), 1, false, false)
v2 = sha1("foofile0")
check(v1 == v2)

