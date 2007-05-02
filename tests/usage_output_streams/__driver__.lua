mtn_setup()

-- --help output goes to stdout
check(mtn("--help"), 0, true, true)
check(qgrep("Usage:", "stdout"))
check(not qgrep("Usage:", "stderr"))

check(mtn("status", "--help"), 0, true, true)
check(qgrep("Usage:", "stdout"))
check(not qgrep("Usage:", "stderr"))

-- but usage errors go to stderr
check(mtn(), 2, true, true)
check(not qgrep("Usage:", "stdout"))
check(qgrep("Usage:", "stderr"))

check(mtn("db"), 1, true, true)
check(not qgrep("no subcommand specified", "stdout"))
check(qgrep("no subcommand specified", "stderr"))
