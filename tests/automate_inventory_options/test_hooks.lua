function ignore_file(name)
   if (name == "source/ignored_1" or
       name == "target/ignored_1") then
       return true
   end
   return false
end
function get_passphrase(keyid)
    return keyid
end
