
function cleanup()
  -- stuff under test isn't cleaned between tests;
  -- only predefined vars get reset
  test.cleanup_ran = true
end
