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
--	include("/PATH/TO/monotone-mirror.lua")
--
--    You may want to change the following variables:
--
--	MM_mirror_dir	The absolute path to the directory where all
--			the mirroring scripts and database are stored.
--	MM_mirror_rcfile
--			The absolute path to the configuration file used
--			by MM_script.
--
--    You may also want to change the following variables, but I wouldn't
--    recommend it:
--
--	MM_mirror_script
--			The absolute path to the mirroring shell script,
--			monotone-mirror.sh.
--			Depends on MM_mirror_dir by default, and should
--			probably not be changed in itself.
--	MM_mirror_database
--			The absolute path to the mirror database.
--			Depends on MM_mirror_dir by default.
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- Variables
-------------------------------------------------------------------------------
MM_default_mirror_dir = "/var/lib/monotone/mirror"
MM_default_mirror_rcfile = "/etc/monotone/mirror.rc"

-- These should normally not be touched.
-- If you have to, make damn sure you know what you do.
if not MM_mirror_dir then MM_mirror_dir = MM_default_mirror_dir end
if not MM_mirror_rcfile then MM_mirror_rcfile = MM_default_mirror_rcfile end
MM_mirror_script = MM_mirror_dir .. "/monotone-mirror.sh"
MM_mirror_database = MM_mirror_dir .. "/mirror.mtn"
MM_mirror_log = MM_mirror_dir .. "/mirror.log"
MM_mirror_errlog = MM_mirror_dir .. "/mirror.err"

-------------------------------------------------------------------------------
-- Local hack of the note_netsync_end function
-------------------------------------------------------------------------------
do
   local saved_note_netsync_end = note_netsync_end
   function note_netsync_end(session_id, status,
			     bytes_in, bytes_out,
			     certs_in, certs_out,
			     revs_in, revs_out,
			     keys_in, keys_out,
			     ...)
      if saved_note_netsync_end then
	 saved_note_netsync_end(session_id, status,
				bytes_in, bytes_out,
				certs_in, certs_out,
				revs_in, revs_out,
				keys_in, keys_out,
				unpack(arg))
      end
      if certs_in > 0 or revs_in > 0 or keys_in > 0 then
	 spawn_redirected("/dev/null", MM_mirror_log, MM_mirror_errlog,
			  MM_mirror_script,MM_mirror_database,MM_mirror_rcfile)
      end
   end
end
