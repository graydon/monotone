-- Test setting conflict resolutions in a conflict file

mtn_setup()

-- Generate a conflicts file, with one conflict for each type of
-- resolution list of resolutions is in roster_merge.cc, syms
-- declaration; any sym that starts with "resolved".
-- 
-- resolution               file
-- resolved_drop_left       checkout_left.sh abe
-- resolved_drop_right      checkout_right.sh beth
-- resolved_rename_right    thermostat.c -> thermostat-honeywell.c
-- resolved_rename_left     thermostat.c -> thermostat-westinghouse.c
-- resolved_user            randomfile
-- resolved_user_left       checkout_left.sh beth
-- resolved_user_right      checkout_right.sh abe
--
-- We can't set 'resolved_internal'.

addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

writefile("randomfile", "randomfile abe 1")
addfile("checkout_left.sh", "checkout_left.sh abe 1")
addfile("checkout_right.sh", "checkout_right.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

writefile("randomfile", "randomfile beth 1")
addfile("checkout_left.sh", "checkout_left.sh beth 1")
addfile("checkout_right.sh", "checkout_right.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit("testbranch", "beth_1")
beth_1 = base_revision()

check (mtn("automate", "show_conflicts"), 0, true, nil)
canonicalize("stdout")
check(samefilestd("conflicts-1", "stdout"))

-- Save the conflicts file so we can edit it 
mkdir("resolutions")
rename("stdout", "resolutions/conflicts")

-- 'resolve_conflict' specifies a resolution for the first unresolved
-- conflict in the file.
resolution = "resolved_drop_left\n resolved_user_right \"resolutions/checkout_left.sh\""
check(mtn("resolve_conflict", "resolutions/conflicts", resolution), 0, nil, nil)

resolution = "resolved_rename_left \"thermostat-westinghouse.c\"\n resolved_rename_right \"thermostat-honeywell.c\""
check(mtn("resolve_conflict", "resolutions/conflicts", resolution), 0, nil, nil)

check(samefilestd("conflicts-2", "resolutions/conflicts"))

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file", "resolutions/conflicts"), 0, true, nil)
canonicalize("stdout")
check(samefilestd("merge-1", "stdout"))

check(mtn("update"), 0, nil, false)

check("checkout_left.sh beth 2" == readfile("checkout_left.sh"))

-- end of file
