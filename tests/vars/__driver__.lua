
mtn_setup()

check(mtn("set", "domain1", "key1", "orig_value"), 0, false, false)
check(mtn("set", "domain1", "key1", "overwritten_value"), 0, false, false)
check(mtn("set", "domain1", "key2", "other_value"), 0, false, false)
check(mtn("set", "domain2", "key1", "other_domain_value"), 0, false, false)
check(mtn("set", "domain2", "key2", "yet_another_value"), 0, false, false)
check(mtn("unset", "domain2", "key2"))
check(mtn("unset", "domain2", "key2"), 1, false, false)

-- FIXME: use a less lame output format
writefile("domain1_vars", "domain1: key1 overwritten_value\n"..
                          "domain1: key2 other_value\n")
writefile("domain2_vars", "domain2: key1 other_domain_value\n")
check(cat("domain1_vars", "domain2_vars"), 0, true)
rename("stdout", "all_vars")
canonicalize("domain1_vars")
canonicalize("domain2_vars")
canonicalize("all_vars")

check(mtn("ls", "vars"), 0, true, false)
canonicalize("stdout")
check(samefile("all_vars", "stdout"))
check(mtn("ls", "vars", "domain1"), 0, true, false)
canonicalize("stdout")
check(samefile("domain1_vars", "stdout"))
check(mtn("ls", "vars", "domain2"), 0, true, false)
canonicalize("stdout")
check(samefile("domain2_vars", "stdout"))
