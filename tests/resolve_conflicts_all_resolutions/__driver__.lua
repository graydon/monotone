-- Test showing and setting all possible conflict resolutions in a
-- conflict file. Also test 'conflict show_remaining'.

mtn_setup()

-- Generate a conflicts file, with one conflict for each type of
-- resolution. The list of currently supported resolutions is in
-- roster_merge.cc, syms declaration; any sym that starts with
-- "resolved".
--
-- resolution               file
-- resolved_drop_left       checkout_left.sh abe
-- resolved_drop_right      checkout_right.sh beth
-- resolved_internal        simple_file
-- resolved_rename_left     thermostat.c -> thermostat-westinghouse.c
-- resolved_rename_right    thermostat.c -> thermostat-honeywell.c
-- resolved_user            user_file
-- resolved_user_left       checkout_left.sh beth
-- resolved_user_right      checkout_right.sh abe
--
-- We can't set 'resolved_internal' directly; it is set by 'conflicts store'.

addfile("simple_file", "simple\none\ntwo\nthree\n")
addfile("user_file", "blah blah blah")
commit()
base = base_revision()

addfile("simple_file", "simple\nzero\none\ntwo\nthree\n")
writefile("user_file", "user_file abe 1")
addfile("checkout_left.sh", "checkout_left.sh abe 1")
addfile("checkout_right.sh", "checkout_right.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

addfile("simple_file", "simple\none\ntwo\nthree\nfour\n")
writefile("user_file", "user_file beth 1")
addfile("checkout_left.sh", "checkout_left.sh beth 1")
addfile("checkout_right.sh", "checkout_right.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit("testbranch", "beth_1")
beth_1 = base_revision()

-- Don't use _MTN/conflicts, to test that capability
mkdir("resolutions")
check (mtn("conflicts", "--conflicts-file=resolutions/conflicts", "store", abe_1, beth_1), 0, nil, nil)
check(samefilestd("conflicts-1", "resolutions/conflicts"))

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_remaining"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_remaining-checkout_left", "stderr"))

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_first-checkout_left", "stderr"))

writefile("resolutions/checkout_left.sh", "checkout_left.sh beth 2")
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_left", "drop"), 0, nil, nil)
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_right", "user", "resolutions/checkout_left.sh"), 0, nil, nil)

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_first-checkout_right", "stderr"))

writefile("resolutions/checkout_right.sh", "checkout_right.sh beth 2")
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_right", "drop"), 0, nil, nil)
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_left", "user", "resolutions/checkout_right.sh"), 0, nil, nil)

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_remaining"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_remaining-thermostat", "stderr"))

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_first-thermostat", "stderr"))

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_left", "rename", "thermostat-westinghouse.c"), 0, nil, nil)
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first_right", "rename", "thermostat-honeywell.c"), 0, nil, nil)

check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_first-user", "stderr"))

writefile("resolutions/user_file", "user_file merged")
check(mtn("conflicts", "--conflicts-file=resolutions/conflicts", "resolve_first", "user", "resolutions/user_file"), 0, nil, nil)

check(samefilestd("conflicts-resolved", "resolutions/conflicts"))

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file", "resolutions/conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

-- Verify user specified resolution files
check(mtn("update"), 0, nil, false)

check("checkout_left.sh beth 2" == readfile("checkout_left.sh"))
check("checkout_right.sh beth 2" == readfile("checkout_right.sh"))
check("user_file merged" == readfile("user_file"))

-- end of file
