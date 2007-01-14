-- This test exercises monotone's ability to detect and recover from errors
-- while migrating to new database schemata.

function test_one(tag, diagnostic)
   dump = tag .. ".dump"
   db = tag .. ".mtn"
   check(get(dump))
   check(raw_mtn("db", "load", "-d", db), 0, nil, nil, {dump})
   check(raw_mtn("db", "migrate", "-d", db), 1, nil, true)
   check(qgrep(diagnostic, "stderr"))
end

test_one("bad_base64", "invalid base64 character")
