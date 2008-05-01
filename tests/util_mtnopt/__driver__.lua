mtn_setup()

mtnopt=getpathof("mtnopt")

-- check default operation
check({mtnopt}, 0, true)
check(qgrep('^MTN_database="'..test.root..'/test.db";$', "stdout"))
check(qgrep('^MTN_branch="testbranch";$', "stdout"))

-- check operation with a specific key and just returning the value
check({mtnopt,'-v','-kbranch'}, 0, true)
check(not qgrep('^'..test.root..'/test.db$', "stdout"))
check(qgrep('^testbranch$', "stdout"))

