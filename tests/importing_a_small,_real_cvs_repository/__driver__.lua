
mtn_setup()

check(get("test.manifest"))

check(get("e"))

check(mtn("--branch=foo.bar", "cvs_import", "e"), 0, false, false)
check(mtn("--branch=foo.bar.disasm-branch", "co"))
check(indir("foo.bar.disasm-branch", mtn("automate", "get_manifest_of")), 0, true)
canonicalize("stdout")
check(samefile("test.manifest", "stdout"))
