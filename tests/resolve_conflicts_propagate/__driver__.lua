-- Test/demonstrate handling of a duplicate name conflict when
-- propagating branches.
--
-- For checkout.sh, the user intent is that there be
-- one file with that name; the contents should be merged.
--
-- For thermostat.c, there should be two files;
-- thermostat-westinghouse.c and thermostat-honeywell.c

mtn_setup()

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit("testbranch")
base = base_revision()

-- Abe adds conflict files and branches
addfile("checkout.sh", "checkout.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit("abe_branch", "abe_1")
abe_1 = base_revision()

revert_to(base)

-- Beth adds files and branches
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit("beth_branch", "beth_1")
beth_1 = base_revision()

-- Propagate abe_branch to beth_branch

check(mtn("conflicts", "store", abe_1, beth_1), 0, true, nil)
check(samefilestd("conflicts-1", "_MTN/conflicts"))

check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "user", "checkout.sh"), 0, nil, nil)

check(mtn("conflicts", "resolve_first_left", "rename", "thermostat-westinghouse.c"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "thermostat-honeywell.c"), 0, nil, nil)

check(samefilestd("conflicts-resolved", "_MTN/conflicts"))

check(mtn("propagate", "--resolve-conflicts", "abe_branch", "beth_branch"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("propagate-1", "stderr"))

check(mtn("conflicts", "clean"), 0, nil, true)

-- Propagate beth_branch to abe_branch

revert_to(abe_1)

check(mtn("automate", "show_conflicts", beth_1, abe_1), 0, true, nil)
canonicalize("stdout")
check(samefilestd("conflicts-2", "stdout"))

check(mtn("conflicts", "store",  beth_1, abe_1), 0, nil, nil)
check(samefilestd("conflicts-2", "_MTN/conflicts"))

check(mtn("propagate", "beth_branch", "abe_branch"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("propagate-2", "stderr"))

-- end of file
