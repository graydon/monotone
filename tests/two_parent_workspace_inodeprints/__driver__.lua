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

sleep(5)

check(fsize("_MTN/inodeprints") == 0)
commit()
check(fsize("_MTN/inodeprints") ~= 0)

check(qgrep("foo", "_MTN/inodeprints"))
check(qgrep("left", "_MTN/inodeprints"))
check(qgrep("right", "_MTN/inodeprints"))

addfile("in-two-parent", "in-two-parent's file content")
check(not qgrep("in-two-parent", "_MTN/inodeprints"))
sleep(5)
commit()
check(qgrep("in-two-parent", "_MTN/inodeprints"))

remove("_MTN/inodeprints")
check(mtn("refresh_inodeprints"), 0, false, false)
check(fsize("_MTN/inodeprints") ~= 0)
check(qgrep("left", "_MTN/inodeprints"))
check(qgrep("right", "_MTN/inodeprints"))
check(qgrep("in-two-parent", "_MTN/inodeprints"))
