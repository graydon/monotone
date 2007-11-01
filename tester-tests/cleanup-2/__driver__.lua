-- the variables set by cleanup-1 should not have survived to this point
check(t_ran == nil)
check(cleanup_ran == nil)
check(test.t_ran == nil)
check(test.cleanup_ran == nil)
