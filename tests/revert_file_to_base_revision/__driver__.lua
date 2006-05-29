
mtn_setup()

addfile("testfile0", "version 0 of first test file\n")
v1 = sha1("testfile0")
commit()

-- check reverting a single file by name

writefile("testfile0", "squirrils monkeys dingos\n")

check(qgrep("squirrils", "testfile0"))
check(cmd(mtn("revert", "testfile0")), 0, false, false)
check(not qgrep("squirrils", "testfile0"))
v2 = sha1("testfile0")
check(v1 == v2)


-- check reverting the whole tree

writefile("testfile0", "squirrils monkeys dingos\n")
check(cmd(mtn("status")), 0, true)
check(qgrep("testfile0", "stdout"))
check(cmd(mtn("revert", ".")), 0, false, false)
check(not exists("_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(not qgrep("testfile0", "stdout"))


-- check reverting a delete

check(cmd(mtn("drop", "testfile0")), 0, false, false)
check(qgrep("testfile0", "_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(qgrep("testfile0", "stdout"))
check(cmd(mtn("revert", ".")), 0, false, false)
check(not exists("_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(not qgrep("testfile0", "stdout"))


-- check reverting a change and a delete

f = io.open("testfile0", "a")
f:write("liver and maude\n")
f:close()
check(cmd(mtn("drop", "testfile0")), 0, false, false)
check(qgrep("testfile0", "_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(qgrep("testfile0", "stdout"))
check(cmd(mtn("revert", "testfile0")), 0, false, false)
check(not exists("_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(not qgrep("testfile0", "stdout"))
v3 = sha1("testfile0")
check(v1 == v3)

-- check reverting an add

addfile("testfile1", "squirrils monkeys dingos\n")
check(qgrep("testfile1", "_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(qgrep("testfile1", "stdout"))
check(cmd(mtn("revert", ".")), 0, false, false)
check(not exists("_MTN/work"))
check(cmd(mtn("status")), 0, true)
check(not qgrep("testfile1", "stdout"))

-- check reverting a directory

mkdir("sub")
addfile("sub/testfile2", "maude\n")
check(cmd(mtn("commit", "--message=new file")), 0, false, false)
writefile("sub/testfile2", "liver\n")
check(cmd(mtn("status")), 0, true)
check(qgrep("sub", "stdout"))
check(cmd(mtn("revert", "sub")), 0, false, false)
check(cmd(mtn("status")), 0, true)
check(not qgrep("sub", "stdout"))

-- it also shouldn't matter how we spell the subdirectory name
writefile("sub/testfile2", "liver\n")
check(cmd(mtn("status")), 0, true)
check(qgrep("sub", "stdout"))
check(cmd(mtn("revert", "sub/")), 0, false, false)
check(cmd(mtn("status")), 0, true)
check(not qgrep("sub", "stdout"))

-- check reverting a missing file
check(cmd(mtn("revert", ".")), 0, false, false)
os.remove("testfile0")
check(cmd(mtn("status")), 1, false, false)
check(cmd(mtn("revert", "testfile0")), 0, true, false)
check(cmd(mtn("status")), 0, false, false)

-- check reverting some changes and leaving others

check(cmd(mtn("revert", ".")), 0, false, false)
check(cmd(mtn("status")), 0, true)

copyfile("testfile0", "foofile0")
copyfile("sub/testfile2", "sub/foofile2")

check(cmd(mtn("rename", "testfile0", "foofile0")), 0, false, false)
check(cmd(mtn("rename", "sub/testfile2", "sub/foofile2")), 0, false, false)

check(cmd(mtn("status")), 0, true)
check(qgrep("foofile0", "stdout"))
check(qgrep("foofile2", "stdout"))
check(exists("_MTN/work"))

check(cmd(mtn("revert", "sub/foofile2")), 0, true)
check(cmd(mtn("status")), 0, true)
check(qgrep("foofile0", "stdout"))
check(not qgrep("foofile2", "stdout"))
check(exists("_MTN/work"))

check(cmd(mtn("revert", "foofile0")), 0, true)
check(cmd(mtn("status")), 0, true)
check(not qgrep("foofile0", "stdout"))
check(not qgrep("foofile2", "stdout"))
check(not exists("_MTN/work"))

-- check that "revert" by itself just prints usage.
writefile("foofile0", "blah\n")
v1 = sha1("foofile0")
check(cmd(mtn("revert")), 2, false, false)
v2 = sha1("foofile0")
check(v1 == v2)

