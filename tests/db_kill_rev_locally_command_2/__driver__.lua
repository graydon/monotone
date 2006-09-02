
mtn_setup()

-- start off with three revisions
addfile("testfile", "blah blah")
commit()
anc = base_revision()

writefile("testfile", "stuff stuff")
commit()
child1 = base_revision()

writefile("testfile", "blahdy blah blay")
commit()
child2 = base_revision()

-- kill head revision
check(mtn("automate", "get_revision", child2), 0, false, false)
check(mtn("db", "kill_rev_locally", child2), 0, false, false)
check(mtn("automate", "get_revision", child2), 1, false, false)
check(mtn("db", "check"), 0, false, false)

-- head is an older revision now, let's kill that too
check(mtn("automate", "get_revision", child1), 0, false, false)
check(mtn("db", "kill_rev_locally", child1), 0, false, false)
check(mtn("automate", "get_revision", child1), 1, false, false)
check(mtn("db", "check"), 0, false, false)
