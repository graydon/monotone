
mtn_setup()

check(get("test.manifest"))

check(get("test.tags"))

check(get("cvs-repository"))

check(mtn("--branch=foo.bar", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("--branch=foo.bar", "checkout"), 0, false, true)
check(indir("foo.bar", mtn("automate", "get_manifest_of")), 0, true)
canonicalize("stdout")
check(samefile("test.manifest", "stdout"))
check(indir("foo.bar", mtn("list", "tags")), 0, true)
canonicalize("stdout")
check(samefile("test.tags", "stdout"))
