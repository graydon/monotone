mtn_setup()

addfile("file1", "ancestor\nancestor")
addfile("file2", "quack quack")
addfile("file3", "t-rex says today is a beautiful day "..
                 "to be stomping on things")
addfile("file4", "don't you see, we're actually all muppets!")
commit()
anc = base_revision()

writefile("file1", "left\nancestor")
writefile("file2", "brawwk brawwk")
commit()
left = base_revision()

revert_to(anc)
writefile("file1", "ancestor\nright")
writefile("file3", "utahraptor asks, is stomping really "..
                   "the answer to your problem(s)?")
commit()
right = base_revision()

check(mtn("merge_into_workspace", left), 0, false, false)

check(mtn("ls", "changed"), 0, "file1\nfile2\nfile3\n", nil)
check(mtn("ls", "known"), 0, "file1\nfile2\nfile3\nfile4\n", nil)

-- these rely on the precise set of junk files that the test suite
-- dumps into the current directory, and on the fact that it doesn't
-- ignore them all.  if the test suite is ever fixed to use a
-- subdirectory for the workspace (and therefore to keep it cleaner)
-- or to ignore them all properly, this will have to change.
check(mtn("ls", "ignored"), 0, "keys\ntest.db\ntest_hooks.lua\nts-stderr\nts-stdin\nts-stdout\n", nil)
check(mtn("ls", "unknown"), 0, "_MTN.old\nmin_hooks.lua\npaths-new\npaths-old\nstderr\ntester.log\n", nil)

-- we do this after the other tests so it doesn't interfere with them.
remove("file4")
check(mtn("ls", "missing"), 0, "file4\n", nil)

-- this is drop because revert doesn't work in a 2-parent workspace yet,
-- and all that matters is we get commit to be happy
check(mtn("drop", "--bookkeep-only", "--missing"), 0, false, false)
commit()
