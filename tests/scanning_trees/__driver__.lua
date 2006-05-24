
mtn_setup()

mkdir("foo")
mkdir("foo/bar")
writefile("testfile0", "version 0 of first test file\n")
writefile("foo/testfile1", "version 0 of second test file\n")
writefile("foo/bar/testfile2", "version 0 of third test file\n")
getfile("manifest")
check(cmd(canonicalize("manifest")))

check(cmd(mtn("add", "testfile0", "foo")), 0, false, false)
check(cmd(commit("testbranch")), 0, false, false)
check(cmd(mtn("automate", "get_manifest_of")), 0, true)
check(cmd(canonicalize("stdout")))
check(cmd(cmp("stdout", "manifest")), 0, false)
