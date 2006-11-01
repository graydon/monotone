function validate_commit_message(message, info, branchname)
  if (not string.find(info, "input.txt")) then
    return false, "Wrong info message"
  end
  if (message == "denyme\n") then
    return false, "input.txt"
  end
  if (not string.find(branchname, "testbranch")) then
    return false, "Wrong branch name"
  end
  return true, ""
end
