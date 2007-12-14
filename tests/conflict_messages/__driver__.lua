mtn_setup()

-- this test creates the various non-content conflict cases
-- and attempts to merge them to check the various messages


-- missing root conflict

branch = "missing"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

mkdir("foo")
addfile("foo/foo", "missing foofoo")
commit(branch)

base = base_revision()

check(mtn("co", "--branch", branch, branch), 0, false, false)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, true, true)
--check(indir(branch, mtn("drop", "--recursive", "bar")), 0, true, true)
check(indir(branch, mtn("commit", "--message", "commit")), 0, false, false)

first = indir(branch, {base_revision})[1]()

check(mtn("drop", "--recursive", "foo"), 0, false, false)

message = "conflict: missing root directory"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- invalid name add

branch = "invalid-add"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

mkdir("foo")
addfile("foo/foo", "invalid add foofoo")
commit(branch)

base = base_revision()

check(mtn("co", "--branch", branch, branch), 0, false, false)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir(branch, mtn("commit", "--message", "commit")), 0, false, false)

first = indir(branch, {base_revision})[1]()

mkdir("foo/_MTN")
addfile("foo/_MTN/foo", "invalid foo")
addfile("foo/_MTN/bar", "invalid bar")

message = "conflict: invalid name"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))


-- invalid name rename

branch = "invalid-rename"

remove("_MTN")
remove("invalid")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

mkdir("foo")
mkdir("bad")
addfile("foo/foo", "invalid rename foofoo")
addfile("bad/_MTN", "invalid bar")
commit(branch)

base = base_revision()

check(mtn("co", "--branch", branch, branch), 0, false, false)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir(branch, mtn("commit", "--message", "commit")), 0, false, false)
first = indir(branch, {base_revision})[1]()

check(mtn("mv", "bad/_MTN", "foo/_MTN"), 0, false, false)

message = "conflict: invalid name"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- directory loop conflict

branch = "loop"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
mkdir("bar")
addfile("foo/foo", "foofoo")
addfile("bar/bar", "barbar")
commit(branch)

base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "bar", "foo"), 0, false, false)

message = "conflict: directory loop"
check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- orphaned add

branch = "orphaned-add"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
addfile("foo/foo", "orphaned add foofoo")
commit(branch)

base = base_revision()

addfile("foo/bar", "orphan foobar")
commit(branch)

check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

message = "conflict: orphaned file"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))


-- orphaned rename

branch = "orphaned-rename"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
addfile("foo/foo", "orphaned rename foofoo")
addfile("bar", "orphaned rename bar")
commit(branch)

base = base_revision()

check(mtn("mv", "bar", "foo/bar"), 0, false, false)
commit(branch)
check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

message = "conflict: orphaned file"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- multiple name conflict

branch = "multiple"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

addfile("foo", "multiple foo")
commit(branch)
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "foo", "baz"), 0, false, false)

message = "conflict: multiple names"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- duplicate name conflict (adds)

branch = "duplicate-adds"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

addfile("foo", "duplicate add foo")
commit(branch)
base = base_revision()

addfile("xxx", "duplicate add xxx")
commit(branch)

check(mtn("mv", "xxx", "bar"), 0, false, false)
--addfile("bar", "duplicate add bar1")
commit(branch)
first = base_revision()

revert_to(base)

addfile("bar", "duplicate add bar2")

message = "conflict: duplicate name"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))


-- duplicate name conflict (renames)

branch = "duplicate-renames"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

addfile("foo", "duplicate rename foo")
addfile("bar", "duplicate rename bar")

commit(branch)
base = base_revision()

check(mtn("mv", "foo", "abc"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "bar", "abc"), 0, false, false)

message = "conflict: duplicate name"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))


-- duplicate name conflict (add-rename)

branch = "duplicate-add-rename"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)

addfile("foo", "duplicate add rename foo")

commit(branch)
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

addfile("bar", "convervent add rename bar")

message = "conflict: duplicate name"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- attribute conflict on attached node

branch = "attribute-attached"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")

addfile("foo", "attribute foo attached")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit(branch)
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "valueX"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "valueY"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)

message = "conflict: multiple values for attribute"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- attribute conflict on detached node

branch = "attribute-detached"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")
remove("bar")
remove("baz")

addfile("foo", "attribute foo detached")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit(branch)
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "valueX"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "valueY"), 0, false, false)
check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "valueZ"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)
check(mtn("mv", "foo", "baz"), 0, false, false)

message = "conflict: multiple values for attribute"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))



-- content conflict on attached node

branch = "content-attached"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")

addfile("foo", "content foo attached")
addfile("bar", "content bar attached\none\ntwo\nthree")
addfile("baz", "content baz attached\naaa\nbbb\nccc")
commit(branch)
base = base_revision()

writefile("foo", "foo first revision")
writefile("bar", "content bar attached\nzero\none\ntwo\nthree")
writefile("baz", "content baz attached\nAAA\nbbb\nccc")
commit(branch)
first = base_revision()

revert_to(base)

writefile("foo", "foo second revision")
writefile("bar", "content bar attached\none\ntwo\nthree\nfour")
writefile("baz", "content baz attached\naaa\nbbb\nCCC")

message = "conflict: content conflict on file 'foo'"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))
check(not qgrep("conflict: content conflict on file 'bar'", "stderr"))
check(not qgrep("conflict: content conflict on file 'baz'", "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))


-- content conflict on detached node

branch = "content-detached"

remove("_MTN")
check(mtn("setup", ".", "--branch", branch), 0, false, false)
remove("foo")

addfile("foo", "content foo detached")
commit(branch)
base = base_revision()

writefile("foo", "foo first revision")
check(mtn("mv", "foo", "bar"), 0, false, false)

commit(branch)
first = base_revision()

revert_to(base)

writefile("foo", "foo second revision")
check(mtn("mv", "foo", "baz"), 0, false, false)

message = "conflict: content conflict on file"

check(mtn("update", "--debug"), 1, false, true)
check(qgrep(message, "stderr"))

commit(branch .. "-propagate")
second = base_revision()

check(mtn("propagate", branch , branch .. "-propagate"), 1, false, true)
check(qgrep(message, "stderr"))
check(mtn("cert", second, "branch", branch))

check(mtn("show_conflicts", first, second), 0, false, true)
check(qgrep(message, "stderr"))

check(mtn("explicit_merge", first, second, branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge", "--branch", branch), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("pluck", "--revision", base, "--revision", first), 1, false, true)
check(qgrep(message, "stderr"))

check(mtn("merge_into_workspace", first), 1, false, true)
check(qgrep(message, "stderr"))
