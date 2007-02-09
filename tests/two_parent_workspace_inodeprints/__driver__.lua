mtn_setup()

addfile("foo", "ancestor\nancestor")
commit()
anc = base_revision()

writefile("foo", "left\nancestor")
addfile("left", "only on left")
commit()
other = base_revision()
remove("left")

revert_to(anc)
writefile("foo", "ancestor\nright")
addfile("right", "only on right")
commit()

check(mtn("merge_into_workspace", other), 0, false, false)

-- check that we've got the expected initial status
check(mtn("status"), 0, true, false)
check(qgrep("patched[ 	]\+foo", "stdout"))

-- enable inodeprints
writefile("_MTN/inodeprints")

check(mtn("status"), 0, true, false)
check(qgrep("patched[ 	]\+foo", "stdout"))

addfile("bar", "this is bar")
sleep(5)

check(fsize("_MTN/inodeprints") == 0)

commit()

check(fsize("_MTN/inodeprints") ~= 0)
