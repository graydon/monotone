mtn_setup()

-- this test creates the various non-content conflict cases and tests
-- that "conflicts show_remaining" can properly process the output of
-- "conflicts store".
--
-- test case generation borrowed from conflict_messages test.


function setup(branch)
    remove("_MTN")
    remove("foo")
    remove("bar")
    remove("baz")
    remove(branch)
    check(mtn("setup", ".", "--branch", branch), 0, false, false)
end

function check_conflicts_left(branch)
    check(mtn("conflicts", "store", "h:" .. branch, "h:" .. branch .. "-propagate"), 0, nil, true)
    check(mtn("conflicts", "show_remaining"), 0, nil, true)
    canonicalize("stderr")
    check(samefilestd(branch .. "_left", "stderr"))
end

function check_conflicts_right(branch)
    check(mtn("conflicts", "store", "h:" .. branch .. "-propagate", "h:" .. branch), 0, nil, true)
    check(mtn("conflicts", "show_remaining"), 0, nil, true)
    canonicalize("stderr")
    check(samefilestd(branch .. "_right", "stderr"))
end

function check_conflicts(branch)
    commit(branch .. "-propagate")
    check_conflicts_left(branch)
    check_conflicts_right(branch)
end

-- missing root conflict

branch = "missing-root"
setup(branch)

mkdir("foo")
addfile("foo/foo", branch .. "-foofoo")
commit(branch)

check(mtn("co", "--branch", branch, branch), 0, nil, nil)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, nil, false)
check(indir(branch, mtn("commit", "--message", "commit")), 0, nil, false)

check(mtn("drop", "--recursive", "foo"), 0, nil, false)

check_conflicts(branch)

-- invalid name add

branch = "invalid-add"
setup(branch)

mkdir("foo")
addfile("foo/foo", branch .. "-foofoo")
commit(branch)

check(mtn("co", "--branch", branch, branch), 0, false, false)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir(branch, mtn("commit", "--message", "commit")), 0, false, false)

mkdir("foo/_MTN")
addfile("foo/_MTN/foo", branch .. "-foo")
addfile("foo/_MTN/bar", branch .. "-bar")

check_conflicts(branch)

-- invalid name rename

branch = "invalid-rename"
setup(branch)

mkdir("foo")
mkdir("bad")
addfile("foo/foo", branch .. "-foofoo")
addfile("bad/_MTN", branch .. "--bar")
commit(branch)

check(mtn("co", "--branch", branch, branch), 0, false, false)
check(indir(branch, mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir(branch, mtn("commit", "--message", "commit")), 0, false, false)

check(mtn("mv", "bad/_MTN", "foo/_MTN"), 0, false, false)

check_conflicts(branch)

-- directory loop conflict

branch = "directory-loop"
setup(branch)

mkdir("foo")
mkdir("bar")
addfile("foo/foo", branch .. "-foofoo")
addfile("bar/bar", branch .. "-barbar")
commit(branch)

base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)

revert_to(base)

check(mtn("mv", "bar", "foo"), 0, false, false)

check_conflicts(branch)


-- orphaned add

branch = "orphaned-add"
setup(branch)

mkdir("foo")
addfile("foo/foo", branch .. "-foofoo")
commit(branch)

base = base_revision()

addfile("foo/bar", branch .. "-foobar")
commit(branch)

check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit(branch)

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check_conflicts(branch)


-- orphaned rename

branch = "orphaned-rename"
setup(branch)

mkdir("foo")
addfile("foo/foo", branch .. "-foofoo")
addfile("bar", branch .. "-bar")
commit(branch)

base = base_revision()

check(mtn("mv", "bar", "foo/bar"), 0, false, false)
commit(branch)
check(mtn("mv", "foo/bar", "foo/baz"), 0, false, false)
commit(branch)

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)

check_conflicts(branch)


-- multiple name conflict

branch = "multiple-names"
setup(branch)

addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)

revert_to(base)

check(mtn("mv", "foo", "baz"), 0, false, false)

check_conflicts(branch)


-- duplicate name conflict (adds)

branch = "duplicate-adds"
setup(branch)

addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

addfile("xxx", branch .. "-xxx")
commit(branch)

check(mtn("mv", "xxx", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

addfile("bar", branch .. "-bar2")

check_conflicts(branch)


-- duplicate name conflict (renames)

branch = "duplicate-renames"
setup(branch)

addfile("foo", branch .. "-foo")
addfile("bar", branch .. "-bar")

commit(branch)
base = base_revision()

check(mtn("mv", "foo", "abc"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "bar", "abc"), 0, false, false)

check_conflicts(branch)


-- duplicate name conflict (add-rename)

branch = "duplicate-add-rename"
setup(branch)

addfile("foo", branch .. "-foo")

commit(branch)
base = base_revision()

check(mtn("mv", "foo", "bar"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

addfile("bar", branch .. "-bar")

check_conflicts(branch)


-- attribute conflict on attached node

branch = "attribute-attached"
setup(branch)

addfile("foo", branch .. "-foo")
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

check_conflicts(branch)


-- attribute conflict on detached node

branch = "attribute-detached"
setup(branch)

addfile("foo", branch .. "-foo")
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

check_conflicts(branch)


-- content conflict on attached node

branch = "content-attached"
setup(branch)

addfile("foo", branch .. "-foo")
addfile("bar", branch .. "-bar\none\ntwo\nthree")
addfile("baz", branch .. "-baz\naaa\nbbb\nccc")
commit(branch)
base = base_revision()

writefile("foo", branch .. "-foo first revision")
writefile("bar", branch .. "-bar\nzero\none\ntwo\nthree")
writefile("baz", branch .. "-baz\nAAA\nbbb\nccc")
commit(branch)
first = base_revision()

revert_to(base)

writefile("foo", branch .. "-foo second revision")
writefile("bar", branch .. "-bar\none\ntwo\nthree\nfour")
writefile("baz", branch .. "-baz\naaa\nbbb\nCCC")

check_conflicts(branch)


-- content conflict on detached node

branch = "content-detached"
setup(branch)

addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

writefile("foo", "foo first revision")
check(mtn("mv", "foo", "bar"), 0, false, false)

commit(branch)
first = base_revision()

revert_to(base)

writefile("foo", "foo second revision")
check(mtn("mv", "foo", "baz"), 0, false, false)

check_conflicts(branch)


-- multiple name plus duplicate name

branch = "multiple-name-plus-duplicate-name"
setup(branch)

addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

check(mtn("mv", "foo", "aaa"), 0, false, false)
addfile("bbb", branch .. "-bbb")

commit(branch)
first = base_revision()

revert_to(base)
remove("aaa")
remove("bbb")

check(mtn("mv", "foo", "bbb"), 0, false, false)
addfile("aaa", branch .. "-aaa")

check_conflicts(branch)


-- multiple name plus orphan

branch = "multiple-name-plus-orphan"
setup(branch)

mkdir("a")
mkdir("b")
check(mtn("add", "a"), 0, false, false)
check(mtn("add", "b"), 0, false, false)
addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

check(mtn("mv", "foo", "a"), 0, false, false)
check(mtn("rm", "b"), 0, false, false)
commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "foo", "b"), 0, false, false)
check(mtn("rm", "a"), 0, false, false)

check_conflicts(branch)


-- multiple name plus directory loop

branch = "multiple-name-plus-directory-loop"
setup(branch)

mkdir("a")
mkdir("b")
mkdir("foo")
check(mtn("add", "a"), 0, false, false)
check(mtn("add", "b"), 0, false, false)
check(mtn("add", "foo"), 0, false, false)
commit(branch)
base = base_revision()

check(mtn("mv", "foo", "a"), 0, false, false)
check(mtn("mv", "b", "a/foo"), 0, false, false)

commit(branch)
first = base_revision()

revert_to(base)

check(mtn("mv", "foo", "b"), 0, false, false)
check(mtn("mv", "a", "b/foo"), 0, false, false)

check_conflicts(branch)


-- duplicate name plus multiple name plus missing root

-- the old root directory is pivoted out to aaa on one side and bbb on the other
-- causing a multiple name conflict

-- a new root directory is pivoted in from foo on one side and bar on the other
-- causing a duplicate name conflict on ""

-- these conflicts leave the root dir detached causing a missing root conflict

branch = "duplicate-name-multiple-name-missing-root"
setup(branch)

mkdir("foo")
mkdir("bar")
addfile("foo/foo", branch .. "-foofoo")
addfile("bar/bar", branch .. "-barbar")
commit(branch)

base = base_revision()

dir1 = branch .. "-1"
remove(dir1)

check(mtn("co", "--revision", base, "--branch", branch, dir1), 0, false, false)
check(indir(dir1, mtn("pivot_root", "foo", "aaa")), 0, true, true)
check(indir(dir1, mtn("commit", "--message", "commit")), 0, false, false)

first = indir(dir1, {base_revision})[1]()

dir2 = branch .. "-2"
remove(dir2)

check(mtn("co", "--revision", base, "--branch", branch, dir2), 0, false, false)
check(indir(dir2, mtn("pivot_root", "bar", "bbb")), 0, true, true)

check(indir(dir2, mtn("commit", "--message", "blah-blah", "--branch", branch .. "-propagate")), 0, false, false)
check_conflicts_left(branch)
check_conflicts_right(branch)


-- unrelated projects

branch = "unrelated-projects"
setup(branch)

copy("_MTN/revision", "clean")

addfile("foo", branch .. "-foo first")
commit(branch)

first = base_revision()

copy("clean", "_MTN/revision")

addfile("foo", branch .. "-foo second")

check_conflicts(branch)

-- end of file
