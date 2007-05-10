-- Create a simple two-parent workspace and then run a bunch of
-- commands all of which should give errors (but not I()s).
-- see http://venge.net/mtn-wiki/MultiParentWorkspaceFallout
-- for rationales for failures

mtn_setup()

addfile("testfile", "ancestor\nancestor")
addfile("otherfile", "blah blah")
commit()
anc = base_revision()

writefile("testfile", "left\nancestor")
writefile("otherfile", "modified too")
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

-- similarly for cat
check(mtn("cat", "testfile"), 1, nil, diag)
check(mtn("automate", "get_file_of", "testfile"), 1, nil, diag)
check(mtn("cat", "-r", left, "testfile"), 0, false, nil)
check(mtn("automate", "get_file_of", "-r", right, "testfile"), 0, false, nil)

-- revert and update: to where?
check(mtn("revert", "."), 1, nil, diag)
check(mtn("update"), 1, nil, diag)

-- formats need updating to deal
check(mtn("automate", "get_base_revision_id"), 1, nil, diag)
check(mtn("automate", "inventory"), 1, nil, diag)
check(mtn("automate", "get_attributes", "testfile"), 1, nil, diag)

-- commit cannot be restricted
check(mtn("commit", "testfile", "--message", "blah-blah"),
      1, nil, true)
check(qgrep("cannot be restricted", "stderr"))

commit() -- unrestricted commit succeeds
