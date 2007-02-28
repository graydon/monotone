
mtn_setup()

writefile("foo", "foo file")
writefile("baz", "baz file")

-- produce root
check(mtn("add", "foo"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("foo")

-- produce move edge
check(mtn("add", "baz"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "foo", "bar"), 0, false, false)
rename("foo", "bar")
commit()

-- revert to root
probe_node("foo", root_r_sha, root_f_sha)
remove("bar")
remove("baz")

-- make a delete edge on the move preimage
check(mtn("drop", "--bookkeep-only", "foo"), 0, false, false)
commit()

-- merge the del and the rename
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)

-- check that the delete landed on the renamed target
check(mtn("automate", "get_manifest_of"), 0, true)
rename("stdout", "manifest")
check(qgrep("baz", "manifest"))
check(not qgrep("bar", "manifest"))
check(not qgrep("foo", "manifest"))
