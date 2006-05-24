
mtn_setup()

mkdir("foo")
mkdir("foo/bar")
writefile("testfile0", "version 0 of first test file\n")
writefile("foo/testfile1", "version 0 of second test file\n")
writefile("foo/bar/testfile2", "version 0 of third test file\n")
getfile("manifest")
check(prepare(canonicalize("manifest")))

check(prepare(mtn("add", "testfile0", "foo")), 0, false, false)
check(prepare(commit("testbranch")), 0, false, false)
check(prepare(mtn("automate", "get_manifest_of")), 0, true)
check(prepare(canonicalize("stdout")))
check(prepare(cmp("stdout", "manifest")), 0, false)
