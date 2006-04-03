-- monotone hooks file

-- This hook reads the passphrases file in the confdir (/etc/monotone)
function get_passphrase (keypair_id)
   local permfile = io.open(get_confdir() .. "/passphrases", "r")
   if (permfile == nil) then
      return false
   end
   local line = permfile:read()
   while (line ~= nil) do
      local _, _, key, passphrase = string.find(line, "%s*([^%s]*)%s*\"(.*)\"%s*")
      if keypair_id == key then return passphrase end
      line = permfile:read()
   end
   io.close(permfile)
   return false
end

