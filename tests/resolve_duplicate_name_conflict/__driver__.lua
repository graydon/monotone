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

check(mtn("conflicts", "store"), 0, true, nil)
check(samefilestd("conflicts-1", "_MTN/conflicts"))

-- Find out what the first unresolved conflict is
check(mtn("conflicts", "show_first"), 0, nil, true)

-- Retrieve Abe's version of checkout.sh, and pretend we did a manual
-- merge, using our favorite merge tool. We put the files in the
-- bookkeeping area, so mtn doesn't see them.
mkdir("_MTN/resolutions")
check(mtn("automate", "get_file", "61b8d4fb0e5d78be111f691b955d523c782fa92e"), 0, true, nil)
rename("stdout", "_MTN/resolutions/checkout.sh-abe")
check(readfile("_MTN/resolutions/checkout.sh-abe") == "checkout.sh abe 1")

writefile("_MTN/resolutions/checkout.sh", "checkout.sh beth 2")

-- specify a part of the resolution for the first unresolved conflict in the file.
check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)

-- and now the other part
check(mtn("conflicts", "show_first"), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "user", "_MTN/resolutions/checkout.sh"), 0, nil, nil)

-- Find out what the next unresolved conflict is
check(mtn("conflicts", "show_first"), 0, nil, true)

check(mtn("conflicts", "resolve_first_left", "rename", "thermostat-westinghouse.c"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "thermostat-honeywell.c"), 0, nil, nil)

check(samefilestd("conflicts-resolved", "_MTN/conflicts"))

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file", "_MTN/conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

check(mtn("conflicts", "clean"), 0, nil, true)

check(mtn("update"), 0, nil, false)

check("checkout.sh beth 2" == readfile("checkout.sh"))

-- end of file
