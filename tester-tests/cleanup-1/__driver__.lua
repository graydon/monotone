-- no state whatsoever is inherited across tests
-- (see cleanup-2 for the other half of this test)

function cleanup()
   cleanup_ran = true
   test.cleanup_ran = true
end

t_ran = true
test.t_ran = true
