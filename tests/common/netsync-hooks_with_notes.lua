logfile = nil

function note_netsync_start(session_id, my_role, sync_type,
                            remote_host, remote_keyname,
                            includes, excludes)
   logfile = io.open("testnotes-" .. my_role .. ".log","w")
   logfile:write(session_id .. " start ---------------------------------------------------\n")
   logfile:write(session_id .. " start: sync_type = " .. sync_type .. "\n")
   logfile:write(session_id .. " start: remote_host = " .. remote_host .. "\n")
   logfile:write(session_id .. " start: remote_keyname = " .. remote_keyname .. "\n")
   logfile:write(session_id .. " start: includes = " .. includes .. "\n")
   logfile:write(session_id .. " start: excludes = " .. excludes .. "\n")
end

function note_netsync_revision_received(new_id, revision, certs, session_id)
   logfile:write(session_id .. " revision: new_id    = " .. new_id .. "\n")
   logfile:write(session_id .. " revision: revision  = " .. revision .. "\n")
   for i, cert in pairs(certs)
   do
      logfile:write(session_id .. " revision: cert.name  = " .. cert.name .. "\n")
      logfile:write(session_id .. " revision: cert.value = " .. cert.value .. "\n")
      logfile:write(session_id .. " revision: cert.key   = " .. cert.key .. "\n")
   end
end

function note_netsync_cert_received(rev_id, key, name, value, session_id)
   logfile:write(session_id .. " cert: rev_id = " .. rev_id .. "\n")
   logfile:write(session_id .. " cert: name   = " .. name .. "\n")
   logfile:write(session_id .. " cert: value  = " .. value .. "\n")
   logfile:write(session_id .. " cert: key    = " .. key .. "\n")
end

function note_netsync_pubkey_received(keyname, session_id)
   logfile:write(session_id .. " pubkey: " .. keyname .. "\n")
end

function note_netsync_end(session_id, status,
                          bytes_in, bytes_out,
                          certs_in, certs_out,
                          revs_in, revs_out,
                          keys_in, keys_out)
   logfile:write(session_id .. " end: status = " .. status .. "\n")
   logfile:write(session_id .. " end: bytes in/out = " .. bytes_in .. "/" .. bytes_out .. "\n")
   logfile:write(session_id .. " end: certs in/out = " .. certs_in .. "/" .. certs_out .. "\n")
   logfile:write(session_id .. " end: revs in/out = " .. revs_in .. "/" .. revs_out .. "\n")
   logfile:write(session_id .. " end: keys in/out = " .. keys_in .. "/" .. keys_out .. "\n")
   logfile:write(session_id .. " end -----------------------------------------------------\n")
   logfile:close()
end

do
   local old = get_netsync_read_permitted
   function get_netsync_read_permitted(pattern, identity)
      local permfile = io.open(get_confdir() .. "/read-permissions", "r")
      if (permfile == nil) then
	 return true
      end
      io.close(permfile)
      return old(pattern, identity)
   end
end

do
   local old = get_netsync_write_permitted
   function get_netsync_write_permitted(identity)
      local permfile = io.open(get_confdir() .. "/write-permissions", "r")
      if (permfile == nil) then
	 return true
      end
      io.close(permfile)
      return old(identity)
   end
end
