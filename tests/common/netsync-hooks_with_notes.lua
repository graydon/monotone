logfile = nil

function note_netsync_start()
   logfile = io.open("testnotes.log","w")
   logfile:write("start ---------------------------------------------------\n")
end

function note_netsync_revision_received(new_id, revision, certs)
   logfile:write("revision: new_id    = " .. new_id .. "\n")
   logfile:write("revision: revision  = " .. revision .. "\n")
   for i, cert in pairs(certs)
   do
      logfile:write("revision: cert.name  = " .. cert.name .. "\n")
      logfile:write("revision: cert.value = " .. cert.value .. "\n")
      logfile:write("revision: cert.key   = " .. cert.key .. "\n")
   end
end

function note_netsync_cert_received(rev_id, key, name, value)
   logfile:write("cert: rev_id = " .. rev_id .. "\n")
   logfile:write("cert: name   = " .. name .. "\n")
   logfile:write("cert: value  = " .. value .. "\n")
   logfile:write("cert: key    = " .. key .. "\n")
end

function note_netsync_pubkey_received(keyname)
   logfile:write("pubkey: " .. keyname .. "\n")
end

function note_netsync_end()
   logfile:write("end -----------------------------------------------------\n")
   logfile:close()
end

function get_netsync_read_permitted(pattern, identity)
	return true
end

function get_netsync_write_permitted(identity)
	return true
end
