function validate_commit_message(message, info)
  if (not string.find(info, "input.txt")) then
    return false, "Wrong info message"
  end
  if (message == "denyme") then
    return false, "input.txt"
  end

  return true, ""
end
