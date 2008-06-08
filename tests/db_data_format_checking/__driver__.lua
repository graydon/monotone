
mtn_setup()

-- A few times in our history we've had to do data migration beyond
-- what the "db migrate" command can handle -- i.e., "db changesetify"
-- and "db rosterify".  We would like for it to be the case, that all
-- other commands give a nice error message if these commands need to
-- be run.  This test makes sure that monotone exits with an error if
-- given a db that appears to be very old.

check(get("changesetify.db.dumped", "stdin"))
check(mtn("-d", "cs-modern.db", "db", "load"), 0, false, false, true)
check(mtn("-d", "cs-modern.db", "db", "migrate"), 0, false, false)

check(mtn("-d", "cs-modern.db", "ls", "keys"), 1, false, false)
check(mtn("-d", "cs-modern.db", "ls", "branches"), 1, false, false)


check(get("rosterify.db.dumped", "stdin"))
check(mtn("-d", "ro-modern.db", "db", "load"), 0, false, false, true)
check(mtn("-d", "ro-modern.db", "db", "migrate"), 0, false, false)

check(mtn("-d", "ro-modern.db", "ls", "keys"), 1, false, false)
check(mtn("-d", "ro-modern.db", "ls", "branches"), 1, false, false)

-- arguably "db regenerate_caches" should go here too -- it's treated
-- similarly.  But the test "schema_migration" tests for its behavior in this
-- case.
