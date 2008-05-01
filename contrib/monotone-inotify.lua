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


if not MI_flagfile then MI_flagfile = MI_default_flagfile end

-------------------------------------------------------------------------------
-- Local hack of the note_netsync_* functions
-------------------------------------------------------------------------------
push_netsync_notifier(
   {
      ["end"] =
	 function (...)
	    local handle = io.open(MI_flagfile, "w+")
	    io.close(handle)
	 end
   })
