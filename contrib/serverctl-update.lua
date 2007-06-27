
do
   local old_trust_hook = get_revision_cert_trust
   function get_revision_cert_trust(signers, id, name, value)
      if name == 'branch' and value == trim(read_conffile("serverctl-branch")) then
	 local trusted = conffile_iterator("serverctl-signers")
	 while trusted:get() do
	    for _, s in pairs(signers) do
	       log("tring "..trim(trusted.line).." for key "..s.."...")
	       if globish_match(trim(trusted.line), s) then
		  trusted:close()
		  return true
	       end
	    end
	 end
	 trusted:close()
	 return false
      else
	 return old_trust_hook(signers, id, name, value)
      end
   end
end

-- We don't want to re-trigger any events.
function note_netsync_start()
   return
end
function note_netsync_revision_received()
   return
end
function note_netsync_cert_received()
   return
end
function note_netsync_end()
   return
end