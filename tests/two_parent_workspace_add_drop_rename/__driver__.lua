mtn_setup()

addfile("foo", "ancestor\nancestor")
commit()
anc = base_revision()

writefile("foo", "left\nancestor")
commit()
other = base_revision()

revert_to(anc)
writefile("foo", "ancestor\nright")
commit()

check(mtn("merge_into_workspace", other), 0, false, false)

writefile("fudgie", "fudgie content")

check(mtn("add", "fudgie"), 0, false, true)
check(qgrep("adding fudgie", "stderr"))

check(mtn("drop", "--bookkeep-only", "fudgie"), 0, false, true)
check(qgrep("dropping fudgie", "stderr"))

check(mtn("rename", "foo", "bar"), 0, false, true)
check(qgrep("renaming foo to bar", "stderr"))

commit()
