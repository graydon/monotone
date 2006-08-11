
mtn_setup()

-- 1) if no --branch is specified, disapprove should use the branch
--    cert on the rev being disapproved.  if there are multiple such
--    certs, it should fail.  the working copy's branch (if any) is
--    irrelevant.
-- 2) if --branch is specified, dispprove should use the branch given,
--    and ignore the branch cert on the rev being disapproved.

-- This test is a bug report.

-- From reading the disapprove code; this is obviously broken.  I don't
-- have time to write a real test right now.  So this is a todo to even
-- write the bug report...
xfail_if(true, false)
