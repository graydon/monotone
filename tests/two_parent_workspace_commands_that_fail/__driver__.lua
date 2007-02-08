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
diffdiag = ("mtn: misuse: this workspace has more than one parent\n"..
	    "mtn: misuse: (specify a revision to diff against with --revision)\n")

check(mtn("merge_into_workspace", anc), 1, nil, diag)

-- diff with no arguments: what parent?
check(mtn("diff"), 1, nil, diffdiag)
check(mtn("automate", "content_diff"), 1, nil, diffdiag)

-- diff can do something sensible if you specify a parent
check(mtn("diff", "-r", left), 0, false, nil)
check(mtn("automate", "content_diff", "-r", right), 0, false, nil)

xfail(mtn("revert"), 1, nil, diag)
xfail(mtn("update"), 1, nil, diag)

xfail(mtn("automate", "get_base_revision_id"), 1, nil, diag)
xfail(mtn("automate", "inventory"), 1, nil, diag)
