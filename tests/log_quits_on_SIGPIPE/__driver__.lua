-- user interaction commands that produce a lot of output, like 'mtn
-- log', should die immediately on SIGPIPE (which they'll get if, for
-- instance, the user pipes the command to a pager, reads the first
-- few dozen lines, and then quits the pager).

skip_if(ostype=="Windows")
SIGPIPE = 13 -- what it is on traditional Unixy systems

mtn_setup()
addfile("file", "This is a file")
commit()

-- Testing this correctly involves avoiding a number of race
-- conditions.  This is how we used to try to do it:
--
--    parent               child
--    ------               -----
--    fork
--                         exec
--                         open pipe for write
--    kill(child, SIGPIPE)
--
-- But it is entirely possible for the parent to complete all of the
-- actions on its side before the child has set up signal handlers,
-- and SIGPIPE may be being ignored, so the signal gets lost.  Also,
-- this is not really testing what we want to test, which is behavior
-- on delivery of a real SIGPIPE by the kernel.  So we want to do this
-- instead:
--
--    parent               child
--    ------               -----
--    fork
--                         exec
--                         open pipe for write
--    open pipe for read
--    close pipe
--                         write to pipe
--                         (kernel generates SIGPIPE)
--
-- There is a synchronization point at the open()s, but the child may
-- still do the write before the parent closes its end of the pipe,
-- causing the signal not to be delivered.  To handle this, we need
-- to introduce a second pipe to use as a semaphore:
--
--    parent               child
--    ------               -----
--    fork
--                         exec
--                         open p1 for write
--    open p1 for read
--                         open p2 for write
--    close p1
--    open p2 for read
--                         write to p1
--                         (kernel generates SIGPIPE)
--
-- It does not matter which side opens p2 for read and which for
-- write, so we have each process open both pipes the same way.
-- The code for the child half of this is in "hookfile".
--
-- Note that if we ever get the ability to redirect stdout in bg(),
-- then we should point it at p1, and have hookfile just synchronize
-- with p2; this tests even more closely the case we care about.

check({"mkfifo", "fifo1"}, 0)
check({"mkfifo", "fifo2"}, 0)
check(get("hookfile"))

proc = bg(mtn("log", "--rcfile=hookfile"), -SIGPIPE, true, true)

p1, e1 = io.open("fifo1", "r")
if p1 == nil then err(e1) end
p1:close()
p2, e2 = io.open("fifo2", "r")
if p2 == nil then err(e2) end
p2:close()

proc:finish(3) -- three second timeout
