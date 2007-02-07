-- Create a simple two-parent workspace and then run a bunch of
-- commands all of which should give errors (but not I()s).
-- see http://venge.net/monotone/wiki/MultiParentWorkspaceFallout
-- for rationales for failures

mtn_setup()

addfile("testfile", "ancestor\nancestor")
commit()
anc = base_revision()

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

diag = "mtn: misuse: this command can only be used in a single-parent workspace\n"

check(mtn("merge_into_workspace", anc), 1, nil, diag)

xfail(mtn("diff"), 1, nil, diag)
xfail(mtn("revert"), 1, nil, diag)
xfail(mtn("update"), 1, nil, diag)

xfail(mtn("automate", "get_base_revision_id"), 1, nil, diag)
xfail(mtn("automate", "inventory"), 1, nil, diag)
