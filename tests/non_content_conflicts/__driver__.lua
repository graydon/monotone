mtn_setup()

-- this test creates the various non-content conflict cases
-- and attempts to merge them to check the various messages



-- divergent name conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "divergent"), 0, false, false)

addfile("foo", "divergent foo")
commit("divergent")
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit("divergent")
other = base_revision()

revert_to(base)

check(mtn("mv", "foo", "baz"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

commit("divergent")

check(mtn("merge", "--branch", "divergent"), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))


-- convergent name conflict (adds)

remove("_MTN")
check(mtn("setup", ".", "--branch", "convergent-adds"), 0, false, false)

addfile("foo", "convergent add foo")
commit("convergent-adds")
base = base_revision()

addfile("xxx", "convergent add xxx")
commit("convergent-adds")

check(mtn("mv", "xxx", "bar"), 0, false, false)
--addfile("bar", "convergent add bar1")
commit("convergent-adds")
other = base_revision()

revert_to(base)

addfile("bar", "convergent add bar2")

check(mtn("update"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("convergent-adds")

check(mtn("merge", "--branch", "convergent-adds"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))


-- convergent name conflict (renames)

remove("_MTN")
check(mtn("setup", ".", "--branch", "convergent-renames"), 0, false, false)

addfile("foo", "convergent rename foo")
addfile("bar", "convergent rename bar")

commit("convergent-renames")
base = base_revision()

check(mtn("mv", "foo", "abc"), 0, false, false)
commit("convergent-renames")
other = base_revision()

revert_to(base)

check(mtn("mv", "bar", "abc"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("convergent-renames")

check(mtn("merge", "--branch", "convergent-renames"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))


-- convergent name conflict (add-rename)

remove("_MTN")
check(mtn("setup", ".", "--branch", "convergent-add-rename"), 0, false, false)

addfile("foo", "convergent add rename foo")

commit("convergent-add-rename")
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit("convergent-add-rename")
other = base_revision()

revert_to(base)

addfile("bar", "convervent add rename bar")

check(mtn("update"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("convergent-add-rename")

check(mtn("merge", "--branch", "convergent-add-rename"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))


-- directory loop conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "loop"), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
mkdir("bar")
addfile("foo/foo", "foofoo")
addfile("bar/bar", "barbar")
commit("loop")

base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit("loop")
other = base_revision()

revert_to(base)

check(mtn("mv", "bar", "foo"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

commit("loop")

check(mtn("merge", "--branch", "loop"), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))


-- orphaned add

remove("_MTN")
check(mtn("setup", ".", "--branch", "orphaned-add"), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
addfile("foo/foo", "orphaned add foofoo")
commit("orphaned-add")

base = base_revision()

addfile("foo/bar", "orphan foobar")
commit("orphaned-add")

check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit("orphaned-add")
other = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

commit("orphaned-add")

check(mtn("merge", "--branch", "orphaned-add"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))


-- orphaned rename

remove("_MTN")
check(mtn("setup", ".", "--branch", "orphaned-rename"), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
addfile("foo/foo", "orphaned rename foofoo")
addfile("bar", "orphaned rename bar")
commit("orphaned-rename")

base = base_revision()

check(mtn("mv", "bar", "foo/bar"), 0, false, false)
commit("orphaned-rename")
check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit("orphaned-rename")
other = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

commit("orphaned-rename")

check(mtn("merge", "--branch", "orphaned-rename"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))


-- invalid name add

remove("_MTN")
check(mtn("setup", ".", "--branch", "invalid-add"), 0, false, false)

mkdir("foo")
addfile("foo/foo", "invalid add foofoo")
commit("invalid-add")

base = base_revision()

check(mtn("co", "--branch", "invalid-add", "invalid"), 0, false, false)
check(indir("invalid", mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir("invalid", mtn("commit", "--message", "commit")), 0, false, false)

other = indir("invalid", {base_revision})[1]()

mkdir("foo/_MTN")
addfile("foo/_MTN/foo", "invalid foo")
addfile("foo/_MTN/bar", "invalid bar")

check(mtn("update"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

commit("invalid-add")

check(mtn("merge", "--branch", "invalid-add"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))


-- invalid name rename

remove("_MTN")
remove("invalid")
check(mtn("setup", ".", "--branch", "invalid-rename"), 0, false, false)

mkdir("foo")
mkdir("bad")
addfile("foo/foo", "invalid rename foofoo")
addfile("bad/_MTN", "invalid bar")
commit("invalid-rename")

base = base_revision()

check(mtn("co", "--branch", "invalid-rename", "invalid"), 0, false, false)
check(indir("invalid", mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir("invalid", mtn("commit", "--message", "commit")), 0, false, false)
other = indir("invalid", {base_revision})[1]()

check(mtn("mv", "bad/_MTN", "foo/_MTN"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

commit("invalid-rename")

check(mtn("merge", "--branch", "invalid-rename"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))



-- missing root conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "missing"), 0, false, false)

mkdir("foo")
addfile("foo/foo", "missing foofoo")
commit("missing")

base = base_revision()

check(mtn("co", "--branch", "missing", "missing"), 0, false, false)
check(indir("missing", mtn("pivot_root", "foo", "bar")), 0, true, true)
--check(indir("missing", mtn("drop", "--recursive", "bar")), 0, true, true)
check(indir("missing", mtn("commit", "--message", "commit")), 0, false, false)

other = indir("missing", {base_revision})[1]()

check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

commit("missing")

check(mtn("merge", "--branch", "missing"), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))



-- attribute conflict on attached node

remove("_MTN")
check(mtn("setup", ".", "--branch", "attribute-attached"), 0, false, false)
remove("foo")

addfile("foo", "attribute foo attached")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit("attribute-attached")
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "valueX"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "valueY"), 0, false, false)
commit("attribute-attached")
other = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

commit("attribute-attached")

check(mtn("merge", "--branch", "attribute-attached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))



-- attribute conflict on detached node

remove("_MTN")
check(mtn("setup", ".", "--branch", "attribute-detached"), 0, false, false)
remove("foo")

addfile("foo", "attribute foo detached")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit("attribute-detached")
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "valueX"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "valueY"), 0, false, false)
check(mtn("mv", "foo", "bar"), 0, false, false)
commit("attribute-detached")
other = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)
check(mtn("mv", "foo", "baz"), 0, false, false)

check(mtn("update"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

commit("attribute-detached")

check(mtn("merge", "--branch", "attribute-detached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", other), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge_into_workspace", other), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))
