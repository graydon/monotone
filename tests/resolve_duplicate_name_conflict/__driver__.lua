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
check(mtn("merge"), 1, false, false)

-- Beth fixes the conflicts.
--
-- For checkout.sh, she retrieves Abe's version to merge with hers,
-- using 'automate get_file_of'. This requires knowing the revision id
-- of Abe's commit, which we get from 'automate show_conflicts'.
--
-- For thermostat.c, she renames her version, letting Abe rename his.

check (mtn("automate", "show_conflicts"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))

check_basic_io_line (1, parsed[1], "left", abe_1) -- 1337..
check_basic_io_line (2, parsed[2], "right", beth_1) -- d5f1..
check_basic_io_line (3, parsed[3], "ancestor", base)

check_basic_io_line (7, parsed[7], "left_file_id", "61b8d4fb0e5d78be111f691b955d523c782fa92e")

-- mtn is not up to actually doing the merge of checkout.sh yet, so we
-- just drop beth's version

check (mtn ("drop", "checkout.sh"), 0, false, false)
check (mtn ("rename", "thermostat.c", "thermostat-honeywell.c"), 0, false, false)
commit()

-- This succeeds
check(mtn("merge"), 0, false, false)

check(mtn("update"), 0, false, false)

check("checkout.sh abe 1" == readfile("checkout.sh"))

-- end of file

