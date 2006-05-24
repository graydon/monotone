
mtn_setup()

writefile("importme", "version 0 of test file\n")

check(cmd(sha1("importme")), 0, true)
tsha = string.gsub(readfile("stdout"), "%s*$", "")

check(cmd(mtn("add", "importme")), 0, false, false)
check(cmd(commit()), 0, false, false)
check(cmd(mtn("automate", "get_file", tsha)), 0, true)
check(cmd(canonicalize("stdout")))
check(cmd(cmp("importme", "stdout")), 0, false)
