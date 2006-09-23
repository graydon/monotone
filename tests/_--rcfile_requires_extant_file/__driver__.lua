
mtn_setup()

-- rcfiles may contain security settings.  So make it a hard error if
-- the user typoes or somesuch.
check(mtn("--rcfile=no-such-file", "status"), 1, false, false)
