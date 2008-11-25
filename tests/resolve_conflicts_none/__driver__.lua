-- Demonstrate handling conflict resolutions file with no conflicts

mtn_setup()

addfile("foo", "foo")
addfile("bar", "bar\none\ntwo\nthree")
addfile("baz", "baz\naaa\nbbb\nccc")
commit("testbranch", "base")
base = base_revision()

writefile("foo", "foo\nfirst\nrevision")
writefile("bar", "bar\nzero\none\ntwo\nthree")
writefile("baz", "baz\nAAA\nbbb\nccc")
commit("testbranch", "first")
first = base_revision()

revert_to(base)

addfile("foobar", "foobar\nfirst\nrevision")
commit("testbranch", "second")
second = base_revision()

check(mtn("conflicts", "store"), 0, nil, nil)
check(samefilestd("conflicts-1", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(readfile("stderr") == "mtn: all conflicts resolved\n")

check(mtn("merge", "--resolve-conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

check(mtn("update"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("update-1", "stderr"))
-- end of file
