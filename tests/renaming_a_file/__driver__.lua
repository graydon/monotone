
mtn_setup()

writefile("foo", "foo file")
writefile("bleh", "bleh file")

-- produce root
addfile("foo")
commit()
root_r_sha = base_revision()
root_f_sha = sha1("foo")

-- produce move edge
check(cmd(mtn("rename", "foo", "bar")), 0, false, false)
copyfile("foo", "bar")
commit()

-- revert to root
probe_node("foo", root_r_sha, root_f_sha)
os.remove("bar")

-- make a simple add edge
addfile("bleh")
commit()

-- merge the add and the rename
check(cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
check(cmd(mtn("automate", "get_manifest_of")), 0, true, false)
os.rename("stdout", "manifest")
check(qgrep("bar", "manifest"))
check(qgrep("bleh", "manifest"))

-- rename a rename target
check(cmd(mtn("rename", "bleh", "blah")), 0, false, false)
check(qgrep("bleh", "_MTN/work"))
check(qgrep("blah", "_MTN/work"))
check(cmd(mtn("rename", "blah", "blyh")), 0, false, false)
check(qgrep("bleh", "_MTN/work"))
check(not qgrep("blah", "_MTN/work"))
check(qgrep("blyh", "_MTN/work"))

-- undo a rename
check(cmd(mtn("rename", "blyh", "bleh")), 0, false, false)
check(not exists("_MTN/work"))

-- move file before renaming it
check(cmd(mtn("status")), 0, false, false)
os.rename("bar", "barfoo")
check(cmd(mtn("rename", "bar", "barfoo")), 0, false, true)
check(qgrep('renaming bar to barfoo in workspace manifest', "stderr"))
check(cmd(mtn("status")), 0, false, false)

-- move file to wrong place before renaming it
os.rename("barfoo", "bar")
check(cmd(mtn("revert", ".")), 0, false, false)
check(cmd(mtn("status")), 0, false, false)
os.rename("bar", "barfoofoo")
check(cmd(mtn("rename", "bar", "barfoo")), 0, false, true)
check(qgrep('renaming bar to barfoo in workspace manifest', "stderr"))
check(cmd(mtn("status")), 1, false, false)
