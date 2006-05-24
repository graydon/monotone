
mtn_setup()

writefile("importme", "version 0 of test file\n")

check(prepare(sha1("importme")), 0, true)
tsha = string.gsub(readfile("stdout"), "%s*$", "")

check(prepare(mtn("add", "importme")), 0, false, false)
check(prepare(commit()), 0, false, false)
check(prepare(mtn("automate", "get_file", tsha)), 0, true)
check(prepare(canonicalize("stdout")))
check(prepare(cmp("importme", "stdout")), 0, false)
