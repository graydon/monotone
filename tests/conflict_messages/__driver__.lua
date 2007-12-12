mtn_setup()

-- this test creates the various non-content conflict cases
-- and attempts to merge them to check the various messages


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

first = indir("missing", {base_revision})[1]()

check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

commit("missing")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("explicit_merge", first, second, "missing"), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("merge", "--branch", "missing"), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: missing root directory", "stderr"))



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

first = indir("invalid", {base_revision})[1]()

mkdir("foo/_MTN")
addfile("foo/_MTN/foo", "invalid foo")
addfile("foo/_MTN/bar", "invalid bar")

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

commit("invalid-add")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("explicit_merge", first, second, "invalid-add"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge", "--branch", "invalid-add"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
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
first = indir("invalid", {base_revision})[1]()

check(mtn("mv", "bad/_MTN", "foo/_MTN"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

commit("invalid-rename")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("explicit_merge", first, second, "invalid-rename"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge", "--branch", "invalid-rename"), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: invalid name", "stderr"))



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
first = base_revision()

revert_to(base)

check(mtn("mv", "bar", "foo"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

commit("loop")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("explicit_merge", first, second, "loop"), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("merge", "--branch", "loop"), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: directory loop", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
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
first = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

commit("orphaned-add")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("explicit_merge", first, second, "orphaned-add"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge", "--branch", "orphaned-add"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
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
first = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

commit("orphaned-rename")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("explicit_merge", first, second, "orphaned-rename"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge", "--branch", "orphaned-rename"), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: orphaned file", "stderr"))



-- multiple name conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "multiple"), 0, false, false)

addfile("foo", "multiple foo")
commit("multiple")
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit("multiple")
first = base_revision()

revert_to(base)

check(mtn("mv", "foo", "baz"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

commit("multiple")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("explicit_merge", first, second, "multiple"), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("merge", "--branch", "multiple"), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: multiple names", "stderr"))



-- duplicate name conflict (adds)

remove("_MTN")
check(mtn("setup", ".", "--branch", "duplicate-adds"), 0, false, false)

addfile("foo", "duplicate add foo")
commit("duplicate-adds")
base = base_revision()

addfile("xxx", "duplicate add xxx")
commit("duplicate-adds")

check(mtn("mv", "xxx", "bar"), 0, false, false)
--addfile("bar", "duplicate add bar1")
commit("duplicate-adds")
first = base_revision()

revert_to(base)

addfile("bar", "duplicate add bar2")

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("duplicate-adds")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("explicit_merge", first, second, "duplicate-adds"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge", "--branch", "duplicate-adds"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))


-- duplicate name conflict (renames)

remove("_MTN")
check(mtn("setup", ".", "--branch", "duplicate-renames"), 0, false, false)

addfile("foo", "duplicate rename foo")
addfile("bar", "duplicate rename bar")

commit("duplicate-renames")
base = base_revision()

check(mtn("mv", "foo", "abc"), 0, false, false)
commit("duplicate-renames")
first = base_revision()

revert_to(base)

check(mtn("mv", "bar", "abc"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("duplicate-renames")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("explicit_merge", first, second, "duplicate-renames"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge", "--branch", "duplicate-renames"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))


-- duplicate name conflict (add-rename)

remove("_MTN")
check(mtn("setup", ".", "--branch", "duplicate-add-rename"), 0, false, false)

addfile("foo", "duplicate add rename foo")

commit("duplicate-add-rename")
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit("duplicate-add-rename")
first = base_revision()

revert_to(base)

addfile("bar", "convervent add rename bar")

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

commit("duplicate-add-rename")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("explicit_merge", first, second, "duplicate-add-rename"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge", "--branch", "duplicate-add-rename"), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: duplicate name", "stderr"))



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
first = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

commit("attribute-attached")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("explicit_merge", first, second, "attribute-attached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge", "--branch", "attribute-attached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))



-- attribute conflict on detached node

remove("_MTN")
check(mtn("setup", ".", "--branch", "attribute-detached"), 0, false, false)
remove("foo")
remove("bar")
remove("baz")

addfile("foo", "attribute foo detached")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit("attribute-detached")
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "valueX"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "valueY"), 0, false, false)
check(mtn("mv", "foo", "bar"), 0, false, false)
commit("attribute-detached")
first = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)
check(mtn("mv", "foo", "baz"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

commit("attribute-detached")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("explicit_merge", first, second, "attribute-detached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge", "--branch", "attribute-detached"), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: multiple values for attribute", "stderr"))



-- content conflict on attached node

remove("_MTN")
check(mtn("setup", ".", "--branch", "content-attached"), 0, false, false)
remove("foo")

addfile("foo", "content foo attached")
addfile("bar", "content bar attached\none\ntwo\nthree")
addfile("baz", "content baz attached\naaa\nbbb\nccc")
commit("content-attached")
base = base_revision()

writefile("foo", "foo first revision")
writefile("bar", "content bar attached\nzero\none\ntwo\nthree")
writefile("baz", "content baz attached\nAAA\nbbb\nccc")
commit("content-attached")
first = base_revision()

revert_to(base)

writefile("foo", "foo second revision")
writefile("bar", "content bar attached\none\ntwo\nthree\nfour")
writefile("baz", "content baz attached\naaa\nbbb\nCCC")

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: content conflict on file 'foo'", "stderr"))
check(not qgrep("conflict: content conflict on file 'bar'", "stderr"))
check(not qgrep("conflict: content conflict on file 'baz'", "stderr"))

commit("content-attached")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("explicit_merge", first, second, "content-attached"), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("merge", "--branch", "content-attached"), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))


-- content conflict on detached node

remove("_MTN")
check(mtn("setup", ".", "--branch", "content-detached"), 0, false, false)
remove("foo")

addfile("foo", "content foo detached")
commit("content-detached")
base = base_revision()

writefile("foo", "foo first revision")
check(mtn("mv", "foo", "bar"), 0, false, false)

commit("content-detached")
first = base_revision()

revert_to(base)

writefile("foo", "foo second revision")
check(mtn("mv", "foo", "baz"), 0, false, false)

check(mtn("update", "--debug"), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

commit("content-detached")
second = base_revision()

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("explicit_merge", first, second, "content-detached"), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("merge", "--branch", "content-detached"), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep("conflict: content conflict on file", "stderr"))
