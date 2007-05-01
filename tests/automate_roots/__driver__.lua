-- -*- lua -*-

revs = {}

mtn_setup()

-- empty graph
check(mtn("automate", "roots"), 0, false, false)

-- add something on first branch
addfile("testfile", "blah blah")
commit()
revs.a = base_revision()

check(mtn("automate", "roots"), 0, true, false)
check(trim(readfile("stdout")) == revs.a)

-- add another rev on that branch
writefile("testfile", "other stuff")
commit()
revs.b = base_revision()


-- now, make a new branch
remove("testfile")
remove("_MTN")
check(mtn("setup", "--branch=otherbranch", "."), 0, false, false)

-- and add something there
addfile("otherfile", "blah blah")
commit()
revs.c = base_revision()
addfile("otherfile", "foo")
commit()
revs.d = base_revision()

-- now check
check(mtn("automate", "roots"), 0, true, false)
check(qgrep(revs.a, "stdout"))
check(qgrep(revs.c, "stdout"))
check(not(qgrep(revs.b, "stdout")))
check(not(qgrep(revs.d, "stdout")))
