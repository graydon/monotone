
include("common/netsync.lua")
mtn_setup()
netsync.setup()

-- This test currently fails because monotone does not clean up it's pid file.
-- This happens because when the monotone server is terminated with a signal it
-- calls its signal handler which then performs a siglongjmp().  The pid file
-- is deleted in the destructor for a pid_file object, and because of the
-- siglongjmp() the destructor is never called.
--
-- Severaly possible solutions exist:
-- - Clean up the pid file manually after handling the signal.
-- - Have the signal handler set a flag (global var or singleton) that is
--   checked periodically that triggers and exception if set instead of using
--   siglongjmp() (my favorite).
-- - Something I have not considered yet.
--
-- -- Matthew Nicholson <matt@matt-land.com>

srv = netsync.start({"--pid-file=mtn.pid"})
check(exists("mtn.pid"))
srv:finish(0)
xfail_if(true, not exists("mtn.pid"))
