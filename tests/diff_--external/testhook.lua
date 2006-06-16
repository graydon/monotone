function external_diff(file_path, data_old, data_new, is_binary, diff_args, rev_old, rev_new)
  io.write("file_path: " .. file_path .. "\n")
  f = io.open("old_version", "w")
  f:write(data_old)
  io.close(f)
  f = io.open("new_version", "w")
  f:write(data_new)
  io.close(f)
  if diff_args == nil then
    io.write("diff_args is NIL\n")
  else
    io.write("diff_args: " .. diff_args .. "\n")
  end
  io.write("rev_old: " .. rev_old .. "\n")
  io.write("rev_new: " .. rev_new .. "\n")
end
