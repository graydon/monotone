mtn_setup()

addfile("testfile", "ancestor\nancestor")
addfile("pluckfile", "quack quack")
commit()
anc = base_revision()

writefile("pluckfile", "brawwk brawwk")
commit()
pluckrev = base_revision()

revert_to(anc)
writefile("testfile", "left\nancestor")
commit()
left = base_revision()

revert_to(anc)
writefile("testfile", "ancestor\nright")
commit()
right = base_revision()

check(mtn("merge_into_workspace", left), 0, false, false)
check(qgrep("left", "testfile"))
check(qgrep("right", "testfile"))
check(not qgrep("ancestor", "testfile"))
check(qgrep("quack quack", "pluckfile"))

check(mtn("pluck", "-r", pluckrev), 0, false, false)
check(qgrep("brawwk brawwk", "pluckfile"))

remove("_MTN/log") -- or commit() will get a bogus failure
commit()
