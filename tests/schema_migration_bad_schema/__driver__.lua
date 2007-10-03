-- This test exercises monotone's ability to identify random SQLite
-- databases which are not suitable for use by monotone.
-- check_sql_schema() makes a five-way distinction: usable, usable
-- with migration, possibly usable by some newer version but not this
-- one, not a monotone database at all, and utterly empty.  Cases 1 and 2
-- are tested elsewhere, so we just do 3-5.

function test_one(tag, expected_setup)
   dump = tag .. ".dump"
   db = tag .. ".mtn"
   check(get(dump))
   check(raw_mtn("db", "load", "-d", db), 0, nil, nil, {dump})

   check(raw_mtn("setup", "-d", db, "-b", "foo", "subdir"), 1, nil, true)
   check(qgrep(expected_setup, "stderr"))
end

test_one("possible", "monotone does not recognize its schema")
test_one("bogus", "does not appear to be a monotone database")
test_one("empty", "empty sqlite database")
