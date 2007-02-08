mtn_setup()

addfile("testfile", "ancestor\nancestor")
addfile("attrfile", "this file has attributes")
commit()
anc = base_revision()

writefile("testfile", "left\nancestor")
check(mtn("attr", "set", "attrfile", "my other car", "made of meat"),
      0, nil, nil)
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
check(mtn("attr", "get", "attrfile", "my other car"),
      0, "attrfile : my other car=made of meat\n", nil)
check(mtn("attr", "set", "testfile", "astral cupcake", "pink frosted"),
      0, nil, nil)
check(mtn("attr", "drop", "attrfile", "my other car"),
      0, nil, nil)

commit()
