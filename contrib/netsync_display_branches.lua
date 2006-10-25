-- Lua snippet to display what branches were affected by revisions and certs
-- that came into the database.  I integrate it into my ~/.monotone/monotonerc
-- /Richard Levitte
--
-- Released as public domain

netsync_branches = {}
function note_netsync_start(nonce)
   netsync_branches[nonce] = {}
end

function note_netsync_revision_received(new_id,revision,certs,nonce)
   for _, item in pairs(certs)
   do
      note_netsync_cert_received(new_id,item.key,item.name,item.value,nonce)
   end
end

function note_netsync_cert_received(rev_id,key,name,value,nonce)
   if name == "branch" then
      netsync_branches[nonce][value] = 1
   end
end

function note_netsync_end(nonce)
   local first = true
   for item, _ in pairs(netsync_branches[nonce])
   do
      if first then
	 io.stderr:write("Affected branches:\n")   
	 first = false
      end
      io.stderr:write("  "..item.."\n")   
   end
   netsync_branches[nonce] = nil
end
