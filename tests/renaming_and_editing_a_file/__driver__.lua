
mtn_setup()

writefile("foo1", "foo file 1")
writefile("foo2", "foo file 2")
writefile("bar1", "bar file 1")
writefile("bar2", "bar file 2")
writefile("bleh", "bleh file")

-- produce root
os.rename("foo1", "foo")
check(cmd(mtn("add", "foo")), 0, false, false)
check(cmd(mtn("--branch=testbranch", "commit", "--message=root")), 0, false, false)
root_r_sha=base_revision()
root_f_sha=sha1("foo")

-- produce 4-step path with move in the middle
os.rename("foo2", "foo")
check(cmd(mtn("commit", "--message=edit-foo")), 0, false, false)
check(cmd(mtn("rename", "foo", "bar")), 0, false, false)
os.rename("bar1", "bar")
check(cmd(mtn("commit", "--message=rename-to-bar")), 0, false, false)
os.rename("bar2", "bar")
check(cmd(mtn("commit", "--message=edit-bar")), 0, false, false)

-- revert to root
probe_node("foo", root_r_sha, root_f_sha)
os.remove("bar")

-- make a simple add edge
check(cmd(mtn("add", "bleh")), 0, false, false)
check(cmd(mtn("commit", "--message=blah-blah")), 0, false, false)

-- merge the add and the rename
check(cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
check(cmd(mtn("automate", "get_manifest_of")), 0, true, false)
os.rename("stdout", "manifest")
check(qgrep("bar", "manifest"))
check(qgrep("bleh", "manifest"))
check(not qgrep("foo", "manifest"))

-- now the moment of truth: do we *think* there was a rename?
check(cmd(mtn("diff", "--revision", root_r_sha)), 0, true, false)
check(qgrep("rename", "stdout"))

os.remove("bar")
