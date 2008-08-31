mtn_setup()

-- Make sure we test the monotone source for mtnopt, not whatever the
-- user happens to have in PATH
getstd ("../mtnopt", "mtnopt")

normalized_testroot = normalize_path (test.root)

-- check default operation

-- MinGW does not process the shebang in mtnopt; must invoke sh directly
-- Vista will probably need to skip this test
check({"/bin/sh", "./mtnopt"}, 0, true)
check(qgrep('^MTN_database="' .. normalized_testroot .. '/test.db";$', "stdout"))
check(qgrep('^MTN_branch="testbranch";$', "stdout"))

-- check operation with a specific key and just returning the value
check({'/bin/sh', './mtnopt', '-v', '-kbranch'}, 0, true)
check(not qgrep('^' .. normalized_testroot .. '/test.db$', "stdout"))
check(qgrep('^testbranch$', "stdout"))

