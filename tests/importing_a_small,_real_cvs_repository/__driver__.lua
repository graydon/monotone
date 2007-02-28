
mtn_setup()

check(get("test.manifest"))

check(get("cvs-repository"))

check(mtn("--branch=foo.bar", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("--branch=foo.bar.disasm-branch", "checkout"), 0, true, true)
check(indir("foo.bar.disasm-branch", mtn("automate", "get_manifest_of")), 0, true)
canonicalize("stdout")
check(samefile("test.manifest", "stdout"))
