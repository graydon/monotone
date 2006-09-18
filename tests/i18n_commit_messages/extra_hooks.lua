function slurp(path)
   local file = io.open(path, "rb")
   local dat = file:read("*a")
   file:close()
   return dat
end

-- this should get the commit message in current locale
function edit_comment(basetext, user_log_message)
   wanted = slurp("euc-jp.txt")

   if string.find(basetext, wanted) ~= nil then
      io.write("EDIT: BASE GOOD\n")
   else
      io.write("EDIT: BASE BAD\n")
   end

   if user_log_message == "" then
      io.write("EDIT: MSG NONESUCH\n")
      return wanted
   else
      if wanted == user_log_message then
         io.write("EDIT: MSG GOOD\n")
      else
         io.write("EDIT: MSG BAD\n")
         io.write(user_log_message)
      end
   end
   return user_log_message
end

-- This should get the commit message in UTF-8
-- If the file "fail_comment" exists, then this causes the commit to fail,
-- so we can check that the write-out-message-to-_MTN/log-on-failure stuff
-- works.
function validate_commit_message(message, revision)
   wanted = slurp("utf8.txt")

   if wanted == message then
      io.write("VALIDATE: MSG GOOD\n")
   else
      io.write("VALIDATE: MSG BAD\n")
      io.write(message)
   end
   
   if string.find(revision, wanted) ~= nil then
      io.write("VALIDATE: REV GOOD\n")
   else
      io.write("VALIDATE: REV BAD\n")
   end

   local file = io.open("fail_comment", "rb")
   if file ~= nil then
      file:close()
      return false, "fail_comment exists"
   else
      return true, ""
   end
end
