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
--    This hook is useful for any system that has a FAM system in
--    place, such as incrond and gamin.  It simply touches a flag
--    file, that can then be used to detect when scripts such as
--    monotone-mirror.sh should be executed.
--
--    in your server's monotonerc, add the following include:
--
--	include("/PATH/TO/monotone-inotify.lua")
--
--    You may want to change the following variables:
--
--	MI_flagfile
--			The absolute path for a file that will simply
--			be touched by the note_netsync_end hook.
--			Default: CONFDIR/monotone-netsync-end.flag
--
--			WARNING!  Do not try to store anything in that
--			file, it will get erased...
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- Variables
-------------------------------------------------------------------------------
MI_default_flagfile = get_confdir() .. "/monotone-netsync-end.flag"
MI_default_debug = false


if not MI_flagfile then MI_flagfile = MI_default_flagfile end
if not MI_debug then MI_debug = MI_default_debug end

-------------------------------------------------------------------------------
-- Local hack of the note_netsync_* functions
-------------------------------------------------------------------------------
push_netsync_notifier(
   {
      ["end"] =
	 function (nonce, status,
		   bytes_in, bytes_out,
		   certs_in, certs_out,
		   revs_in, revs_out,
		   keys_in, keys_out,
		   ...)
	    if MI_debug then
	       io.stderr:write("note_netsync_end: ",
			       string.format("%d certs, %d revs, %d keys",
					     certs_in, revs_in, keys_in),
			       "\n")
	    end
	    if certs_in > 0 or revs_in > 0 or keys_in > 0 then
	       if MI_debug then
		  io.stderr:write("note_netsync_end: touching ", MI_flagfile, "\n")
	       end
	       local handle = io.open(MI_flagfile, "w+")
	       io.close(handle)
	    end
	 end
   })
