-- Test 'automate show_conflicts'

mtn_setup()

check(get("expected-1.stdout"))
check(get("expected-2.stdout"))

--  Get a non-empty base revision, then create conflicts
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict files
addfile("checkout.sh", "checkout.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit()
abe_1 = base_revision()

revert_to(base)

-- Beth adds conflict files
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell")
commit()
beth_1 = base_revision()

check(mtn("automate", "show_conflicts"), 0, true, false)
canonicalize("stdout")
check(readfile("expected-1.stdout") == readfile("stdout"))

-- Now specify revisions, in an order that reverses left/right from
-- the previous, to show the arguments are used.

check(mtn("automate", "show_conflicts", beth_1, abe_1), 0, true, false)
canonicalize("stdout")
check(readfile("expected-2.stdout") == readfile("stdout"))


-- end of file
