-- This test exercises monotone's ability to detect and recover from
-- errors while migrating to new database schemata.  We attempt to be
-- exhaustive in the set of possible errors, but not the set of
-- possible operations that could fail.

function test_one(tag, target, diagnostic)
   dump = tag .. ".dump"
   db = tag .. ".mtn"
   check(get(dump))
   check(raw_mtn("db", "load", "-d", db), 0, nil, nil, {dump})
   check(raw_mtn("db", "test_migration_step", target, "-d", db), 1, nil, true)
   check(qgrep(diagnostic, "stderr"))
end


-- migrate_files_BLOB
test_one("bad_base64", "1db80c7cee8fa966913db1a463ed50bf1b0e5b0e", 
	 "invalid base64 character")
test_one("tmp_in_the_way", "1db80c7cee8fa966913db1a463ed50bf1b0e5b0e",
	 "already another table")
test_one("column_missing", "1db80c7cee8fa966913db1a463ed50bf1b0e5b0e",
	 "no such column")

-- migrate_rosters_no_hash
test_one("no_revision_roster", "9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df",
	 "no such table")

-- migrate_add_heights
test_one("heights_already", "ae196843d368d042f475e3dadfed11e9d7f9f01e",
	 "heights already exists")

-- migrate_to_binary_hashes
test_one("bad_hexenc", "7ca81b45279403419581d7fde31ed888a80bd34e",
	 "invalid hex character")
test_one("short_hexenc", "7ca81b45279403419581d7fde31ed888a80bd34e",
	 "result is the wrong length")

-- I would like to exercise migrate_to_external_privkeys' own peculiar
-- failure mode but I don't see how to trip the "key mismatch" error
-- without having it demand passphrases that don't exist (because the
-- keys are mismatched)...
