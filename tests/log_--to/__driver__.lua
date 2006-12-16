
mtn_setup()

addfile("file", "contents")
commit("testbranch", "comment1")

firstrev = base_revision()

addfile("file2", "quack")
commit("testbranch", "comment2")
check(mtn("tag", base_revision(), "mytag"), 0, false, false)

check(mtn("update", "-r", firstrev), 0, false, false)

addfile("file3", "foo")
commit("testbranch", "comment3")

check(mtn("merge"), 0, false, false)

check(mtn("update"), 0, false, false)

addfile("file4", "q")
commit("testbranch", "comment4")

check(mtn("log", "--to", "t:mytag"), 0, true, false)

check(not qgrep("comment2", "stdout"))
check(qgrep("comment3", "stdout"))
check(qgrep("comment4", "stdout"))
check(not qgrep("comment1", "stdout"))
