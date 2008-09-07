
mtn_setup()

addfile("unique", "unique")
commit()

addfile("testfile", "blah blah")
commit()

writefile("testfile", "foo foo")
commit()

addfile("testfile2", "blah blah")
commit()

writefile("testfile2", "foo foo")
commit()

addfile("workspace_1", "foo foo");
addfile("workspace_2", "hello hello");
addfile("workspace_3", "hello hello");
addfile("workspace_4", "another unique file");

check(mtn("ls", "duplicates"), 0, true, false)
check(qgrep("testfile", "stdout"))
check(qgrep("testfile2", "stdout"))

check(not qgrep("unique", "stdout"))

check(qgrep("workspace_1", "stdout"))
check(qgrep("workspace_2", "stdout"))
check(qgrep("workspace_3", "stdout"))
check(not qgrep("workspace_4", "stdout"))

