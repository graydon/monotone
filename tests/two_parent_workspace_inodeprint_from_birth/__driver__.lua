mtn_setup()

-- enable inodeprints
writefile("_MTN/inodeprints")

-- create common ancestor
addfile("foo", "ancestor\nancestor")
addfile("bar", "bar content")
sleep(5)
check(not qgrep("foo", "_MTN/inodeprints"))
commit()
check(qgrep("foo", "_MTN/inodeprints"))
anc = base_revision()

-- create first child from ancestor (left)
writefile("foo", "left\nancestor")
addfile("left", "only on left")
check(qgrep("foo", "_MTN/inodeprints"))
check(not qgrep("left", "_MTN/inodeprints"))
sleep(5)
commit()
check(qgrep("foo", "_MTN/inodeprints"))
check(qgrep("left", "_MTN/inodeprints"))
check(not qgrep("right", "_MTN/inodeprints"))
other = base_revision()
remove("left")

-- create second child from ancestor (right)
revert_to(anc)
sleep(5)
check(mtn("refresh_inodeprints"), 0, false, false)

check(qgrep("foo", "_MTN/inodeprints"))
check(not qgrep("right", "_MTN/inodeprints"))
writefile("foo", "ancestor\nright")
addfile("right", "only on right")
sleep(5)
check(qgrep("foo", "_MTN/inodeprints"))
check(not qgrep("right", "_MTN/inodeprints"))
commit()
check(not qgrep("left", "_MTN/inodeprints"))
check(qgrep("right", "_MTN/inodeprints"))
check(qgrep("foo", "_MTN/inodeprints"))

-- now create a two parent workspace
check(mtn("merge_into_workspace", other), 0, false, false)

check(fsize("_MTN/inodeprints") ~= 0)

-- foo is changed in the workspace, so it shouldn't be inodeprinted
check(not qgrep("foo", "_MTN/inodeprints"))

-- bar was only touched in the common ancestor
check(qgrep("bar", "_MTN/inodeprints"))

-- left and right are unchanged, but added in the other parent, so should be
-- inodeprinted, except that left was added during the merge_into_workspace,
-- so it might be too new to be inodeprinted (depending on the speed the
-- test happens to run), so we don't check for it.
check(qgrep("right", "_MTN/inodeprints"))

-- wait a bit, refresh, and check that left shows up now
sleep(5)
check(mtn("refresh_inodeprints"))
check(qgrep("left", "_MTN/inodeprints"))

-- test explicit refresh_inodeprints
addfile("in-two-parent", "in-two-parent's file content")
sleep(5)
check(mtn("refresh_inodeprints"), 0, false, false)
check(fsize("_MTN/inodeprints") ~= 0)
check(qgrep("in-two-parent", "_MTN/inodeprints"))
