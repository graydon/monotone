-- This hook reads the 'passphrases' file from the confdir.  It expects the
-- file to be formatted as follows:
--
-- key@domain.tld "passphrase here"
--
-- One entry per line.  The quotes are required.
--
-- Note: Because the file contains passphrases it should only be readable by
-- select users.
function get_passphrase (keypair_id)
   local permfile = io.open(get_confdir() .. "/passphrases", "r")
   if (permfile == nil) then return false end
   local line = permfile:read()
   while (line ~= nil) do
      local _, _, key, passphrase = string.find(line, "%s*([^%s]*)%s*\"(.*)\"%s*")
      if keypair_id == key then return passphrase end
      line = permfile:read()
   end
   io.close(permfile)
   return false
end
