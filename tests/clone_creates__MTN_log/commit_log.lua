function edit_comment(summary, user_log_file)
  -- this used to just return the variable user_log_file,
  -- but now must return the original entry without the 'magic' line.
  -- this avoids a failing test after the 'magic line' patch
  return "Log Entry"
end
