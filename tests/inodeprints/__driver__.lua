
mtn_setup()

-- Turn them on
writefile("_MTN/inodeprints")

check(mtn("diff"), 0, true, false)
check(qgrep("no changes", "stdout"))

addfile("testfile", "blah blah")

-- We scatter these sleep()'s here, to make sure that the inodeprints will
-- actually be saved -- since we do not cache inodeprints for files that have
-- been modified very recently.
sleep(5)

check(mtn("diff"), 0, true, false)
check(qgrep("blah blah", "stdout"))

-- Should still be empty, because there is nothing unchanged.
check(fsize("_MTN/inodeprints") == 0)

commit()

-- Something should have been written to it now
check(fsize("_MTN/inodeprints") ~= 0)

-- And things should still work
check(mtn("diff"), 0, true, false)
check(qgrep("no changes", "stdout"))
copy("_MTN/inodeprints", "ip1")

-- Changes are still detected
writefile("testfile", "stuff stuff")
check(mtn("diff"), 0, true, false)
check(qgrep("stuff stuff", "stdout"))
-- Should have changed the inodeprints file
check(mtn("refresh_inodeprints"), 0, false, false)
check(not samefile("_MTN/inodeprints", "ip1"))

-- Make sure partial commit doesn't screw things up
addfile("otherfile", "other stuff")
sleep(5)
check(mtn("commit", "otherfile", "--message=foo"), 0, false, false)

-- Still should think testfile is modified
check(mtn("diff"), 0, true, false)
check(qgrep("stuff stuff", "stdout"))
