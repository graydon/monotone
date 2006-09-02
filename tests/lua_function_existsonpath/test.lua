-- Need a way to break into the lua interpreter; our strategy is to
-- redefine the use_inodeprints function to run our code, and then
-- create a new workspace.

function use_inodeprints()
  if (existsonpath("ls") == 0 or existsonpath("xcopy") == 0) then
    io.write("asdfghjkl\n")
  end
  if (existsonpath("weaohriosfaoisd") ~= 0) then
    io.write("qwertyuiop\n")
  end
  return false
end
