-- Demonstrate content conflict resolutions
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

writefile("foo", "foo\nsecond\nrevision")
writefile("bar", "bar\none\ntwo\nthree\nfour")
writefile("baz", "baz\nAaa\nbbb\nCCC")
commit("testbranch", "second")
second = base_revision()

check(mtn("automate", "show_conflicts", first, second), 0, true, nil)
canonicalize("stdout")
check(samefilestd("conflicts-1", "stdout"))

writefile("foo", "foo\nmerged\nrevision")
mkdir("_MTN/result")
writefile("_MTN/result/baz", "baz\nAaa\nBbb\nCcc")

-- foo and baz can't be handled by the internal line merger. We
-- specify one user file in _MTN, one out, to ensure mtn handles both.
check(get("resolve-conflicts-1", "_MTN/conflicts"))
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

check(mtn("update"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("update-1", "stderr"))

check(readfile("foo") == "foo\nmerged\nrevision")
check(readfile("bar") == "bar\nzero\none\ntwo\nthree\nfour\n")
check(readfile("baz") == "baz\nAaa\nBbb\nCcc")
-- end of file
