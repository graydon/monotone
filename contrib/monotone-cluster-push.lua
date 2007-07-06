-- Copyright (c) 2007 by Richard Levitte <richard@levitte.org>
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions
-- are met:
--
-- 1. Redistributions of source code must retain the above copyright
--    notice, this list of conditions and the following disclaimer.
--
-- 2. Redistributions in binary form must reproduce the above copyright
--    notice, this list of conditions and the following disclaimer in the
--    documentation and/or other materials provided with the distribution.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
-- Usage:
--
--    NOTE: THIS SOFTWARE IS ONLY MEANT FOR SERVER PROCESSES!
--    Anything else will fail miserably!
--
--    in your server's monotonerc, add the following include:
--
--	include("/PATH/TO/monotone-cluster-push.lua")
--
--    You may want to change the following variables:
--
--	MCP_rcfile
--			The absolute path to the configuration file used
--			by this script.  It contains basio stanza with the
--			following keys:
--
--				pattern		Branch pattern, as in
--						read-permissions.  MUST
--						come first in each stanza.
--				server		Server address or address:port
--						to which this pattern should
--						be pushed.  There may be many
--						of these.
--
--			The default is cluster-push.rc located in the server's
--			configuration directory.
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- Variables
-------------------------------------------------------------------------------
MCP_default_rcfile = get_confdir() .. "/cluster-push.rc"


if not MCP_rcfile then MCP_rcfile = MCP_default_rcfile end

-------------------------------------------------------------------------------
-- Local hack of the note_netsync_* functions
-------------------------------------------------------------------------------
do
   local debug = false

   local branches = {}

   local saved_note_netsync_start = note_netsync_start
   function note_netsync_start(nonce, ...)
      if saved_note_netsync_start then
	 saved_note_netsync_start(nonce,
				  unpack(arg))
      end
      if debug then
	 io.stderr:write("note_netsync_start: initialise branches\n")
      end
      branches[nonce] = {}
   end

   local saved_note_netsync_cert_received = note_netsync_cert_received
   function note_netsync_cert_received(rev_id, key, name, value, nonce, ...)
      if saved_note_netsync_cert_received then
	 saved_note_netsync_cert_received(rev_id, key, name, value, nonce,
					  unpack(arg))
      end
      if debug then
	 io.stderr:write("note_netsync_cert_received: cert ", name,
			 " with value ", value, " received\n")
      end
      if name == "branch" then
	 if debug then
	    io.stderr:write("note_netsync_cert_received: branch ", value,
			    " identified\n")
	 end
	 branches[nonce][value] = true
      end
   end

   local saved_note_netsync_revision_received = note_netsync_revision_received
   function note_netsync_revision_received(new_id, revision, certs, nonce, ...)
      if saved_note_netsync_revision_received then
	 saved_note_netsync_revision_received(new_id, revision, certs, nonce,
					      unpack(arg))
      end
      for _, item in pairs(certs)
      do
	 if debug then
	    io.stderr:write("note_netsync_revision_received: cert ", item.name,
			    " with value ", item.value, " received\n")
	 end
	 if item.name == "branch" then
	    if debug then
	       io.stderr:write("note_netsync_revision_received: branch ",
			       item.value, " identified\n")
	    end
	    branches[nonce][item.value] = true
	 end
      end
   end

   local saved_note_netsync_end = note_netsync_end
   function note_netsync_end(nonce, status,
			     bytes_in, bytes_out,
			     certs_in, certs_out,
			     revs_in, revs_out,
			     keys_in, keys_out,
			     ...)
      if saved_note_netsync_end then
	 saved_note_netsync_end(nonce, status,
				bytes_in, bytes_out,
				certs_in, certs_out,
				revs_in, revs_out,
				keys_in, keys_out,
				unpack(arg))
      end
      if debug then
	 io.stderr:write("note_netsync_end: ",
			 string.format("%d certs, %d revs, %d keys",
				       certs_in, revs_in, keys_in),
			 "\n")
      end
      if certs_in > 0 or revs_in > 0 or keys_in > 0 then
	 if debug then
	    io.stderr:write("note_netsync_end: reading ", MCP_rcfile, "\n")
	 end
	 local rcfile = io.open(MCP_rcfile, "r")
	 if (rcfile == nil) then
	    io.stderr:write("file ", MCP_rcfile, " cannot be opened\n")
	    return false
	 end
	 local dat = rcfile:read("*a")
	 io.close(rcfile)
	 if debug then
	    io.stderr:write("note_netsync_end: got this:\n", dat, "\n")
	 end	 
	 local res = parse_basic_io(dat)
	 if res == nil then
	    io.stderr:write("file ", MCP_rcfile, " cannot be parsed\n")
	    return false
	 end

	 local matches = false
	 local patterns = {}
	 local previous_name = ""
	 for i, item in pairs(res) do
	    if item.name == "pattern" then
	       if debug then
		  io.stderr:write("note_netsync_end: found ", item.name,
				  " = \"", item.values[1], "\"\n")
	       end
	       if previous_name ~= "pattern" then
		  if debug then
		     io.stderr:write("note_netsync_end: clearing matches and patterns because previous_name = \"", previous_name, "\"\n")
		  end
		  matches = false
		  patterns = {}
	       end
	       local pattern = item.values[1]
	       for branch, b in pairs(branches[nonce]) do
		  if debug then
		     io.stderr:write("note_netsync_end: trying to match branch ",
				     branch, "\n")
		  end
		  if globish_match(pattern, branch) then
		     if debug then
			io.stderr:write("note_netsync_end: it matches branch ",
					branch, "\n")
		     end
		     matches = true
		     patterns[pattern] = true
		  end
	       end
	    elseif matches then
	       if item.name == "server" then
		  if debug then
		     io.stderr:write("note_netsync_end: found ", item.name,
				     " = \"", item.values[1], "\"\n")
		  end
		  local server = item.values[1]
		  for pattern, b in pairs(patterns) do
		     io.stderr:write("pushing pattern \"", pattern,
				     "\" to server ", server, "\n")
		     server_request_sync("push", server, pattern, "")
		  end
	       end
	    end
	    previous_name = item.name
	 end
      end
   end

   local saved_note_mtn_startup = note_mtn_startup
   function note_mtn_startup(...)
      if saved_note_mtn_startup then
	 saved_note_mtn_startup(unpack(arg))
      end

      if debug then
	 io.stderr:write("note_mtn_startup: reading ", MCP_rcfile,
			 "\n")
      end
      local rcfile = io.open(MCP_rcfile, "r")
      if (rcfile == nil) then
	 io.stderr:write("file ", MCP_rcfile, " cannot be opened\n")
	 return false
      end
      local dat = rcfile:read("*a")
      io.close(rcfile)
      if debug then
	 io.stderr:write("note_mtn_startup: got this:\n", dat, "\n")
      end	 
      local res = parse_basic_io(dat)
      if res == nil then
	 io.stderr:write("file ", MCP_rcfile, " cannot be parsed\n")
	 return false
      end

      local patterns = {}
      local previous_name = ""
      for i, item in pairs(res) do
	 if item.name == "pattern" then
	    if debug then
	       io.stderr:write("note_mtn_startup: found ", item.name, " = \"",
			       item.values[1], "\"\n")
	    end
	    if previous_name ~= "pattern" then
	       if debug then
		     io.stderr:write("note_mtn_startup: clearing patterns because previous_name = \"", previous_name, "\"\n")
	       end
	       patterns = {}
	    end
	    patterns[item.values[1]] = true
	 elseif item.name == "server" then
	    if debug then
	       io.stderr:write("note_mtn_startup: found ", item.name, " = \"",
			       item.values[1], "\"\n")
	    end
	    local server = item.values[1]
	    for pattern, b in pairs(patterns) do
	       io.stderr:write("pushing pattern \"", pattern, "\" to server ",
			       server, "\n")
	       server_request_sync("push", server, pattern, "")
	    end
	 end
	 previous_name = item.name
      end
      return nil
   end
end
