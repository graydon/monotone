-- Lua snippet to display what branches were affected by revisions and certs
-- that came into the database.  It will display each branch name and the
-- amount of times they each appeared.
-- I integrate it into my ~/.monotone/monotonerc
-- /Richard Levitte
--
-- Released as public domain

do
   netsync_branches = {}
   function _note_netsync_cert_received(rev_id,key,name,value,nonce)
      if name == "branch" then
	 if netsync_branches[nonce][value] == nil then
	    netsync_branches[nonce][value] = 1
	 else
	    netsync_branches[nonce][value] = netsync_branches[nonce][value] + 1
	 end
      end
   end

   netsync_sent_branches = {}
   function _note_netsync_cert_sent(rev_id,key,name,value,nonce)
      if name == "branch" then
	 if netsync_sent_branches[nonce][value] == nil then
	    netsync_sent_branches[nonce][value] = 1
	 else
	    netsync_sent_branches[nonce][value] = netsync_sent_branches[nonce][value] + 1
	 end
      end
   end

   push_netsync_notifier(
      {
	 ["start"] =
	    function (nonce)
	       netsync_branches[nonce] = {}
	       netsync_sent_branches[nonce] = {}
	    end,
	 ["revision_received"] =
	    function (new_id,revision,certs,nonce)
	       for _, item in pairs(certs)
	       do
		  _note_netsync_cert_received(new_id,item.key,item.name,item.value,nonce)
	       end
	    end,
	 ["revision_sent"] =
	    function (new_id,revision,certs,nonce)
	       for _, item in pairs(certs)
	       do
		  _note_netsync_cert_sent(new_id,item.key,item.name,item.value,nonce)
	       end
	    end,
	 ["cert_received"] =
	    function (rev_id,key,name,value,nonce)
	       _note_netsync_cert_received(rev_id,key,name,value,nonce)
	    end,
	 ["cert_sent"] =
	    function (rev_id,key,name,value,nonce)
	       _note_netsync_cert_sent(rev_id,key,name,value,nonce)
	    end,
	 ["end"] =
	    function (nonce, status)
	       -- only try to display results if we got
	       -- at least partial contents
	       if status > 211 then
		  return
	       end

	       local first = true
	       for item, amount in pairs(netsync_branches[nonce])
	       do
		  if first then
		     io.stderr:write("Received data on branches:\n")   
		     first = false
		  end
		  io.stderr:write("  ",item,"  (",amount,")\n")   
	       end
	       netsync_branches[nonce] = nil

	       first = true
	       for item, amount in pairs(netsync_sent_branches[nonce])
	       do
		  if first then
		     io.stderr:write("Sent data on branches:\n")   
		     first = false
		  end
		  io.stderr:write("  ",item,"  (",amount,")\n")   
	       end
	       netsync_sent_branches[nonce] = nil
	    end
      })
end
