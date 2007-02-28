
mtn_setup()

writefile("foo1", "foo file 1")
writefile("foo2", "foo file 2")
writefile("bar1", "bar file 1")
writefile("bar2", "bar file 2")
writefile("bleh", "bleh file")

-- produce root
rename("foo1", "foo")
check(mtn("add", "foo"), 0, false, false)
check(mtn("--branch=testbranch", "commit", "--message=root"), 0, false, false)
root_r_sha=base_revision()
root_f_sha=sha1("foo")

-- produce 4-step path with move in the middle
rename("foo2", "foo")
check(mtn("commit", "--message=edit-foo"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "foo", "bar"), 0, false, false)
rename("bar1", "bar")
check(mtn("commit", "--message=rename-to-bar"), 0, false, false)
rename("bar2", "bar")
check(mtn("commit", "--message=edit-bar"), 0, false, false)

-- revert to root
probe_node("foo", root_r_sha, root_f_sha)
remove("bar")

-- make a simple add edge
check(mtn("add", "bleh"), 0, false, false)
check(mtn("commit", "--message=blah-blah"), 0, false, false)

-- merge the add and the rename
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(mtn("automate", "get_manifest_of"), 0, true, false)
rename("stdout", "manifest")
check(qgrep("bar", "manifest"))
check(qgrep("bleh", "manifest"))
check(not qgrep("foo", "manifest"))

-- now the moment of truth: do we *think* there was a rename?
check(mtn("diff", "--revision", root_r_sha), 0, true, false)
check(qgrep("rename", "stdout"))

remove("bar")
