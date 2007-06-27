-- utility functions, could be moved to std_hooks.lua
function read_conffile(name)
   return read_contents_of_file(get_confdir() .. "/" .. name, "r")
end

function read_basic_io_conffile(name)
   local dat = read_conffile(name)
   if dat == nil then return nil end
   return parse_basic_io(dat)
end

function conffile_iterator(name)
   local out = {}
   out.file = io.open(get_confdir() .. "/" .. name, "r")
   if out.file == nil then return nil end
   local mt = {}
   mt.__index = mt
   mt.get = function()
	       if out.file == nil then return nil end
	       out.line = out.file:read()
	       return out.line
	    end
   mt.close = function()
		 if out.file == nil then return end
		 io.close(out.file)
	      end
   return setmetatable(out, mt)
end

function trim(str)
   local _,_,r = string.find(str, "%s*([^%s]*)%s*")
   return r
end

--------------------------------------------

----------------------------------------
-- files (all under get_confdir())
--   serverctl.lua
--     this file, renamed to or included by monotonerc
--
--   serverctl-branch
--     the name of the control branch
--
--   serverctl-signers
--     list of keys that are allowed to sign revisions
--     in the control branch
--
--   serverctl-update.sh
--     pull to serverctl.mtn and update serverctl/
--
--   serverctl-update.lua
--     trust hook for the updater to use (reads serverctl-signers)
--
--   serverctl.mtn
--     db used by serverctl-update.sh
--
--   serverctl/serverlist
--     one line for each server in the cluster, look like
--       server "address" "key"
--
--   serverctl/include
--   serverctl/exclude
--     branch patterns to share among the cluster
--
--   serverctl/write-permissions
--     list of people (keys) allowed to sync to the server
--     This is IN ADDITION TO anyone allowed by the server's
--     write-permissions .
--
--   serverctl/suggested-signers
--     suggest contents of serverctl-signers


function log(line)
   io.stderr:write(get_confdir() .. ": " .. line .. "\n")
end

do
   local old_write_permitted = get_netsync_write_permitted
   function get_netsync_write_permitted(ident)
      local committers = conffile_iterator("serverctl/write-permissions")
      if committers ~= nil then
	 while committers:get() do
	    if globish_match(trim(committers.line), ident) then
	       committers:close()
	       return true
	    end
	 end
	 committers:close()
      end
      return old_write_permitted(ident)
   end
end


-- Only sync to the rest of the pool if the incoming revs
-- were not received from a member of the pool.
function server_maybe_request_sync(triggerkey)
   log("received something...")
   local data = read_basic_io_conffile("serverctl/serverlist")
   if data == nil then
      io.stderr:write("server list file cannot be parsed\n")
      return
   end

   for i, item in pairs(data)
   do
      if item.name == "server" then
	 if item.values[2] == triggerkey
	 then
	    return
	 end
      end
   end

   local include = trim(read_conffile("serverctl/include"))
   local exclude = trim(read_conffile("serverctl/exclude"))

   log("syncing to other servers...")
   for i, item in pairs(data)
   do
      if item.name == "server" then
	 server_request_sync("sync", item.values[1],
			     include, exclude)
      end
   end
end

function note_netsync_start(sid, role, what, rhost, rkey, include, exclude)
   if sessions == nil then sessions = {} end
   sessions[sid] = {
      key = rkey,
      branches = {},
      include = include,
      exclude = exclude
   }
end

function note_netsync_revision_received(rid, rdat, certs, sid)
   for _, cert in pairs (certs) do
      if cert.name == "branch" then
	 sessions[sid].branches[cert.value] = true
      end
   end
end

function note_netsync_cert_received(rid, key, name, value, sid)
   if name == "branch" then
      sessions[sid].branches[value] = true
   end
end

function note_netsync_end(sid, status, bi, bo, ci, co, ri, ro, ki, ko)
   if ci > 0 or ri > 0 or ki > 0 then
      server_maybe_request_sync(sessions[sid].key)
   elseif sessions[sid].include == '' and
          sessions[sid].exclude == 'ctl-branch-updated' then
      log("resyncing after a config update...")
      server_maybe_request_sync('')
   end
   
   -- Do we update the control checkout?
   local ctlbranch = trim(read_conffile("serverctl-branch"))
   if ctlbranch and sessions[sid].branches[ctlbranch] then
      log("updating configuration...")
      execute(get_confdir() .. "/serverctl-update.sh", get_confdir())
   end
   sessions[sid] = nil
end
