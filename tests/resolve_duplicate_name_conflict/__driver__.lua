-- Test/demonstrate handling of a duplicate name conflict; Abe and
-- Beth add files with the same names.
--
-- For checkout.sh, the user intent is that there be
-- one file with that name; the contents should be merged.
--
-- For thermostat.c, there should be two files;
-- thermostat-westinghouse.c and thermostat-honeywell.c

mtn_setup()
include ("common/test_utils_inventory.lua")

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict files
addfile("checkout.sh", "checkout.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

-- Beth adds files, and attempts to merge
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit("testbranch", "beth_1")
beth_1 = base_revision()

-- This fails due to duplicate name conflicts
check(mtn("merge"), 1, nil, false)

-- Beth fixes the conflicts.
--
-- For checkout.sh, she retrieves Abe's version to merge with hers,
-- using 'automate get_file'. This requires knowing the file id
-- of Abe's commit, which we get from 'automate show_conflicts'.
-- 
-- mtn is not up to actually doing the merge of checkout.sh yet (that
-- requires sutures), so we specify a conflict resolution that drops
-- Abe's version and keeps Beth's manual merge.
--
-- For thermostat.c, she specifies a conflict resolution that renames
-- both versions.

check (mtn("automate", "show_conflicts"), 0, true, nil)
canonicalize("stdout")
check(samefilestd("conflicts-1", "stdout"))

-- Save the conflicts file so we can edit it later
mkdir("resolutions")
rename("stdout", "resolutions/conflicts")

-- Retrieve Abe's version of checkout.sh, and pretend we did a manual
-- merge, using our favorite merge tool. We put the files outside the
-- workspace, so 'update' doesn't complain.
check(mtn("automate", "get_file", "61b8d4fb0e5d78be111f691b955d523c782fa92e"), 0, true, nil)
rename("stdout", "resolutions/checkout.sh-abe")
check(readfile("resolutions/checkout.sh-abe") == "checkout.sh abe 1")

writefile("resolutions/checkout.sh", "checkout.sh beth 2")

-- 'resolve_conflict' specifies a resolution for the first unresolved
-- conflict in the file.
resolution = "resolved_drop_left\n resolved_user_right \"resolutions/checkout.sh\""
check(mtn("resolve_conflict", "resolutions/conflicts", resolution), 0, nil, nil)

resolution = "resolved_rename_left \"thermostat-westinghouse.c\"\n resolved_rename_right \"thermostat-honeywell.c\""
check(mtn("resolve_conflict", "resolutions/conflicts", resolution), 0, nil, nil)

check(samefilestd("conflicts-2", "resolutions/conflicts"))

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file", "resolutions/conflicts"), 0, true, nil)
canonicalize("stdout")
check(samefilestd("merge-1", "stdout"))

check(mtn("update"), 0, nil, false)

check("checkout.sh beth 2" == readfile("checkout.sh"))

-- end of file
