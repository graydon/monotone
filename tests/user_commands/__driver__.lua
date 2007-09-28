mtn_setup()

check(get("extra_rc"))

addfile("foo", "random info\n")
commit()
rev_a = base_revision()

check(mtn("check_head", "--rcfile=extra_rc", rev_a), 0, true, false)

check(samelines("stdout", {"heads are equal", "end of command"}))
