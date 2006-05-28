function merge3(anc_path, left_path, right_path, merged_path, ancestor, left, right)
   return left
end

function get_passphrase(key) return "bob" end

function get_author(branch) return "bob" end

function get_revision_cert_trust(signers, id, name, val)
   for k,v in pairs(signers) do 
        if (v ~= "alice") then return true end
   end
   return false
end
