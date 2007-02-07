
skip_if(ostype == "Windows")
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

function netsync_killpipe(obj)
  kill(obj.pid, 13)
end

writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
commit()

writefile("testfile", "version 1 of test file")
commit()

srv = netsync.start()

-- send the server a SIGPIPE signal (it should survive)
netsync_killpipe(srv)

-- this will fail if the SIGPIPE terminated it
srv:pull("testbranch")

srv:stop()
