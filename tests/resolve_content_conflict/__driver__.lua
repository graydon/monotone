-- Demonstrate content conflict resolutions
--
-- All files in 'files' directory, all commands invoked there, to show
-- that 'conflicts store' uses the right bookkeeping directory.

mtn_setup()

mkdir("files")
addfile("files/foo", "foo")
addfile("files/bar", "bar\none\ntwo\nthree")
addfile("files/baz", "baz\naaa\nbbb\nccc")
commit("testbranch", "base")
base = base_revision()

writefile("files/foo", "foo\nfirst\nrevision")
writefile("files/bar", "bar\nzero\none\ntwo\nthree")
writefile("files/baz", "baz\nAAA\nbbb\nccc")
commit("testbranch", "first")
first = base_revision()

revert_to(base)

writefile("files/foo", "foo\nsecond\nrevision")
writefile("files/bar", "bar\none\ntwo\nthree\nfour")
writefile("files/baz", "baz\nAaa\nbbb\nCCC")
commit("testbranch", "second")
second = base_revision()

check(indir("files", mtn("conflicts", "store", first, second)), 0, nil, nil)
check(samefilestd("conflicts-1", "_MTN/conflicts"))

-- foo and baz can't be handled by the internal line merger. We
-- specify one user file in _MTN, one out, to ensure mtn handles both.
writefile("files/foo", "foo\nmerged\nrevision")
mkdir("_MTN/result")
writefile("_MTN/result/baz", "baz\nAaa\nBbb\nCcc")

check(indir("files", mtn("conflicts", "resolve_first", "user", "../_MTN/result/baz")), 0, nil, nil)
check(indir("files", mtn("conflicts", "resolve_first", "user", "foo")), 0, nil, nil)
check(samefilestd("conflicts-2", "_MTN/conflicts"))

check(mtn("merge", "--resolve-conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

check(mtn("update"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("update-1", "stderr"))

check(readfile("files/foo") == "foo\nmerged\nrevision")
check(readfile("files/bar") == "bar\nzero\none\ntwo\nthree\nfour\n")
check(readfile("files/baz") == "baz\nAaa\nBbb\nCcc")
-- end of file
