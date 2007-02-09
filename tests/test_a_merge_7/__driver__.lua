
mtn_setup()

-- this used to be a test for the --lca option to merge and propagate.
-- this option is no longer needed in 0.26, so has since been removed.
-- --lca was a temporary workaround for 3-way merge suckiness.

-- arguably the issue is now fixed with mark-merge. I've changed 
-- the test to do a successful merge of E and F, since, well,
-- extra merge cases never hurt. 

--    A
--   / \
--  B   C
--  |\ /|
--  D X |
--  |/ \|
--  E   F
revs = {}

addfile("testfile", "foo bar")
commit()
revs.a = base_revision()

addfile("otherfile1", "blah blah")
commit()
revs.b = base_revision()

revert_to(revs.a)
remove("otherfile1")

writefile("testfile", "new stuff")
commit()
revs.c = base_revision()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.f = base_revision()

revert_to(revs.b)

addfile("otherfile2", "blah blah")
commit()
revs.d = base_revision()

check(mtn("explicit_merge", revs.d, revs.c, "testbranch"), 0, false, false)
check(mtn("update"), 0, false, false)
revs.e = base_revision()

check(mtn("merge"), 0, false, true)
check(not qgrep(revs.a, "stderr"))
check(qgrep(revs.e, "stderr"))
check(qgrep(revs.f, "stderr"))
