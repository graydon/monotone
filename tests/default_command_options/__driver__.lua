
mtn_setup()

check(get("default_options.lua"))

check(mtn("version", "--full"), 0, true, false)
rename("stdout", "fullversion")

check(mtn("version", "--rcfile=default_options.lua"), 0, true, false)
check(samefile("stdout", "fullversion"))

check(mtn("status", "--rcfile=default_options.lua"), 1, false, false)

