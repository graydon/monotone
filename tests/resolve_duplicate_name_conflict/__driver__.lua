-- Test/demonstrate handling of a duplicate name conflict; Abe and
-- Beth add files with the same names.
--
-- For checkout.sh, the user intent is that there be
-- one file with that name; the contents should be merged.
--
-- For thermostat.c, there should be two files;
-- thermostat-westinghouse.c and thermostat-honeywell.c

mtn_setup()

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict files
addfile("checkout.sh", "checkout.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit()
abe_1 = base_revision()

revert_to(base)

-- Beth adds files, and attempts to merge
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit()
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

check (mtn("automate show_conflicts"), 0, false, false)

check (mtn ("drop", "checkout.sh"), 0, false, false)
check (mtn ("rename", "thermostat.c", "thermostat-honeywell.c"), 0, false, false)
commit()

-- This succeeds
check(mtn("merge"), 0, false, false)

-- end of file

