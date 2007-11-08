mtn_setup()

-- this test creates the various non-content conflict cases
-- and attempts to merge them to check the various messages

-- divergent name conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "divergent"), 0, false, false)

addfile("foo", "divergent")
commit("divergent")
base = base_revision()

check(mtn("mv", "foo", "left"), 0, false, false)
commit("divergent")

revert_to(base)

check(mtn("mv", "foo", "right"), 0, false, false)
commit("divergent")

check(mtn("merge", "--branch", "divergent"), 1, false, true)
check(qgrep("divergent name conflict", "stderr"))

-- convergent name conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "convergent"), 0, false, false)

addfile("foo", "convergent")
commit("convergent")
base = base_revision()

addfile("bar", "foobar")
commit("convergent")

revert_to(base)

addfile("bar", "barfoo")
commit("convergent")

check(mtn("merge", "--branch", "convergent"), 1, false, true)
check(qgrep("convergent name conflict", "stderr"))

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

revert_to(base)

check(mtn("mv", "bar", "foo"), 0, false, false)
commit("loop")

check(mtn("merge", "--branch", "loop"), 1, false, true)
check(qgrep("directory loop conflict", "stderr"))

-- orphaned node conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "orphan"), 0, false, false)
remove("foo")
remove("bar")

mkdir("foo")
addfile("foo/foo", "foofoo")
commit("orphan")

base = base_revision()

addfile("foo/bar", "foobar")
addfile("foo/baz", "foobaz")
mkdir("foo/sub")
addfile("foo/sub/bar", "foosubbar")
addfile("foo/sub/baz", "foosubbaz")

commit("orphan")

revert_to(base)

remove("foo")
check(mtn("drop", "--recursive", "foo"), 0, false, false)
commit("orphan")

check(mtn("merge", "--branch", "orphan"), 1, false, true)
check(qgrep("orphaned node conflict", "stderr"))

-- illegal name conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "illegal"), 0, false, false)

mkdir("foo")
addfile("foo/foo", "foofoo")
commit("illegal")

base = base_revision()

check(mtn("co", "--branch", "illegal", "illegal"), 0, false, false)
check(indir("illegal", mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir("illegal", mtn("commit", "--message", "commit")), 0, false, false)

mkdir("foo/_MTN")
addfile("foo/_MTN/foo", "foofoo")
addfile("foo/_MTN/bar", "foobar")
commit("illegal")

check(mtn("merge", "--branch", "illegal"), 1, false, true)
check(qgrep("illegal name conflict", "stderr"))

-- missing root conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "missing"), 0, false, false)

mkdir("foo")
addfile("foo/foo", "foofoo")
commit("missing")

base = base_revision()

check(mtn("co", "--branch", "missing", "missing"), 0, false, false)
check(indir("missing", mtn("pivot_root", "foo", "bar")), 0, true, true)
check(indir("missing", mtn("drop", "--recursive", "bar")), 0, true, true)
check(indir("missing", mtn("commit", "--message", "commit")), 0, false, false)

check(mtn("drop", "--recursive", "foo"), 0, false, false)
commit("missing")

check(mtn("merge", "--branch", "missing"), 1, false, true)
check(qgrep("missing root conflict", "stderr"))

-- attribute conflict

remove("_MTN")
check(mtn("setup", ".", "--branch", "attribute"), 0, false, false)
remove("foo")

addfile("foo", "attribute")
check(mtn("attr", "set", "foo", "attr1", "value1"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "value2"), 0, false, false)
commit("attribute")
base = base_revision()

check(mtn("attr", "set", "foo", "attr1", "left-value"), 0, false, false)
check(mtn("attr", "set", "foo", "attr2", "left-value"), 0, false, false)
commit("attribute")

revert_to(base)

check(mtn("attr", "set", "foo", "attr1", "right-value"), 0, false, false)
check(mtn("attr", "drop", "foo", "attr2"), 0, false, false)
commit("attribute")

check(mtn("merge", "--branch", "attribute"), 1, false, true)
check(qgrep("attribute conflict", "stderr"))

