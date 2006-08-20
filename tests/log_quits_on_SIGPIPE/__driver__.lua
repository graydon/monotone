-- user interaction commands that produce a lot of output, like 'mtn
-- log', should die immediately on SIGPIPE (which they'll get if, for
-- instance, the user pipes the command to a pager, reads the first
-- few dozen lines, and then quits the pager).

skip_if(ostype=="Windows")
SIGPIPE = 13 -- what it is on traditional Unixy systems

mtn_setup()
addfile("file", "This is a file")
commit(nil, -- branch name
       "This is the commit message that never ends\n"..
       "It just goes on and on my friends\n"..
       "Some people started reading it not knowing what it was\n"..
       "And they'll be reading it forever and ever just because\n"..
       "This is the commit message that never ends\n")

check({'mkfifo', 'thefifo'}, 0)
check(get("hookfile"))
proc = bg(mtn('log', '--rcfile=hookfile'), -SIGPIPE, true, true)
kill(proc.pid, SIGPIPE)
proc:finish(3) -- three second timeout
