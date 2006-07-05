
mtn_setup()

check(mtn("automate", "interface_version"), 0, true, false)
rename("stdout", "a_v")

-- MinGW's wc produces "      1" as output.  Arithmetic comparison works, string comparison doesn't
check(numlines("a_v") == 1)
-- This is really ^[0-9]+\.[0-9]+$, but m4 is obfuscatory.
check(qgrep("^[0-9]+\.[0-9]+$", "a_v"))
