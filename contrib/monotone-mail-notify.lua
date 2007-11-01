-- Create a file "notify" next to "read-permissions" and ensure
-- its contents are in the same format as "read-permissions",
-- except that the values for allow and deny must be real email
-- addresses.
--
-- This will splat out files in _base. Use the .sh file from
-- cron to process those files
--
-- Copyright (c) 2007, Matthew Sackman (matthew at wellquite dot org)
--                     LShift Ltd (http://www.lshift.net)
--                     Thomas Keller <me@thomaskeller.biz>
--                     Whoever wrote the function "get_netsync_read_permitted"
-- License: GPLv2 or later

_from = "monotone@my.domain.please.change.me"
_base = "/tmp/notify/"

function get_notify_recipients(branch)
   local emailfile = io.open(get_confdir() .. "/notify", "r")
   if (emailfile == nil) then return nil end
   local dat = emailfile:read("*a")
   io.close(emailfile)
   local res = parse_basic_io(dat)
   if res == nil then
      io.stderr:write("file notify cannot be parsed\n")
      return nil
   end

   local results = {}
   local denied = {}
   local matches = false
   local cont = false
   for i, item in pairs(res)
   do
      -- legal names: pattern, allow, deny, continue
      if item.name == "pattern" then
         if matches and not cont then return table.toarray(results) end
         matches = false
         cont = false
         for j, val in pairs(item.values) do
            if globish_match(val, branch) then matches = true end
         end
      elseif item.name == "allow" then if matches then
         for j, val in pairs(item.values) do
            if nil == denied[val] then results[val] = val end
         end
      end elseif item.name == "deny" then if matches then
         for j, val in pairs(item.values) do
            denied[val] = val
         end
      end elseif item.name == "continue" then if matches then
         cont = true
         for j, val in pairs(item.values) do
            if val == "false" or val == "no" then cont = false end
         end
      end elseif item.name ~= "comment" then
         io.stderr:write("unknown symbol in notify: " .. item.name .. "\n")
      end
   end
   return table.toarray(results)
end

function table.toarray(t)
   local t1 = {}
   for j, val in pairs(t) do
      table.insert(t1, val)
   end
   return t1
end

_emails_to_send = {}

function note_netsync_start (session_id, my_role, sync_type, remote_host, remote_keyname, includes, excludes)
    _emails_to_send[session_id] = {}
end

function note_netsync_revision_received (new_id, revision, certs, session_id)
    if _emails_to_send[session_id] == nil then
        -- no session present
        return
    end

    local rev_data = {["certs"] = {}, ["revision"] = new_id, ["manifest"] = revision}
    for _,cert in ipairs(certs) do
        if cert["name"] == "branch" then
           rev_data["recipients"] = get_notify_recipients(cert["value"])
        end
        if cert["name"] ~= nil then
           if nil == rev_data["certs"][cert["name"]] then
              rev_data["certs"][cert["name"]] = {}
           end
           table.insert(rev_data["certs"][cert["name"]], cert["value"])
        end
    end
    _emails_to_send[session_id][new_id] = rev_data
end

do
   local saved_note_netsync_end = note_netsync_end

   function note_netsync_end (session_id, status, bytes_in, bytes_out, certs_in, certs_out, revs_in, revs_out, keys_in, keys_out, ...)
      if saved_note_netsync_end then
         saved_note_netsync_end(session_id, status,
                                bytes_in, bytes_out,
                                certs_in, certs_out,
                                revs_in, revs_out,
                                keys_in, keys_out,
                                unpack(arg))
      end


      if _emails_to_send[session_id] == nil then
         -- no session present
         return
      end

      if status ~= 200 then
         -- some error occured, no further processing takes place
         return
      end

      if _emails_to_send[session_id] == "" then
         -- we got no interesting revisions
         return
      end

      for rev_id,rev_data in pairs(_emails_to_send[session_id]) do
         if # (rev_data["recipients"]) > 0 then
            local subject = make_subject_line(rev_data)
            local reply_to = ""
            for j,auth in pairs(rev_data["certs"]["author"]) do
               reply_to = reply_to .. auth
               if j < # (rev_data["certs"]["author"]) then reply_to = reply_to .. ", " end
            end

            local now = os.time()

            local outputFileRev = io.open(_base .. rev_data["revision"] .. now .. ".rev.txt", "w+")
            local outputFileHdr = io.open(_base .. rev_data["revision"] .. now .. ".hdr.txt", "w+")

            local to = ""
            for j,addr in pairs(rev_data["recipients"]) do
               to = to .. addr
               if j < # (rev_data["recipients"]) then to = to .. ", " end
            end

            outputFileHdr:write("BCC: " .. to .. "\n")
            outputFileHdr:write("From: " .. _from .. "\n")
            outputFileHdr:write("Subject: " .. subject .. "\n")
            outputFileHdr:write("Reply-To: " .. reply_to .. "\n")
            outputFileHdr:close()

            outputFileRev:write(summarize_certs(rev_data))
            outputFileRev:close()
         end
      end

      _emails_to_send[session_id] = nil
   end
end

function summarize_certs(t)
   local str = "revision:            " .. t["revision"] .. "\n"
   local changelog
   for name,values in pairs(t["certs"]) do
      local formatted_value = ""
      for j,val in pairs(values) do
         formatted_value = formatted_value .. name .. ":"
         if string.match(val, "\n")
         then formatted_value = formatted_value .. "\n"
         else formatted_value = formatted_value .. (string.rep(" ", 20 - (# name))) end
         formatted_value = formatted_value .. val .. "\n"
      end
      if name == "changelog" then changelog = formatted_value else str = str .. formatted_value end
   end
   if nil ~= changelog then str = str .. changelog end
   return (str .. "manifest:\n" .. t["manifest"])
end

function make_subject_line(t)
   local str = ""
   for j,val in pairs(t["certs"]["branch"]) do
      str = str .. val
      if j < # t["certs"]["branch"] then str = str .. ", " end
   end
   return str .. ": " .. t["revision"]
end

function table.print(T)
        local done = {}
        local function tprint_r(T, prefix)
                for k,v in pairs(T) do
                        print(prefix..tostring(k),'=',tostring(v))
                        if type(v) == 'table' then
                                if not done[v] then
                                        done[v] = true
                                        tprint_r(v, prefix.."  ")
                                end
                        end
                end
        end
        done[T] = true
        tprint_r(T, "")
end
