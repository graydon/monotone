
mtn_setup()

addfile("foo", "foo\n")
addfile("bar", "bar\n")

commit()
REV1=base_revision()

writefile("foo", "foo changed\n")

commit()
REV2=base_revision()

addfile("quux", "quux\n")

commit()
REV3=base_revision()

writefile("foo", "foo again\n")
writefile("bar", "bar again\n")

check(mtn("rename", "foo", "foo2"), 0, false, false)

commit()
REV4=base_revision()

-- without restrictions
check(mtn("log", "--diffs", "--no-graph"), 0, true, false)
check(grep('^(---|\\+\\+\\+) ', "stdout"), 0, true, false)
rename("stdout", "full")
check(get("expect_full"))
canonicalize("full")
check(samefile("expect_full", "full"))

-- restrict to foo2 and quux
check(mtn("log", "--no-graph", "quux", "--diffs", "foo2"), 0, true, false)
check(grep("^(---|\\+\\+\\+) ", "stdout"), 0, true, false)
rename("stdout", "restrict")
check(get("expect_restrict"))
canonicalize("restrict")
check(samefile("expect_restrict", "restrict"))
