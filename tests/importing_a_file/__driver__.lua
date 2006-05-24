
mtn_setup()

writefile("importme", "version 0 of test file\n")

tsha = sha1("importme")

check(cmd(mtn("add", "importme")), 0, false, false)
commit()
check(cmd(mtn("automate", "get_file", tsha)), 0, true)
canonicalize("stdout")
check(samefile("importme", "stdout"))
