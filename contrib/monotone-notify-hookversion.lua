-- Copyright (c) 2006 by Richard Levitte <richard@levitte.org>
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
-- This software depends on the existence of the following programs:
--
--    mtn
--    sed
--    mime-construct
--    mailx
-------------------------------------------------------------------------------
-- Usage:
--
--    in your monotonerc (or the monotonerc intended for the particular
--    process), add the following include:
--
--        include("/PATH/TO/monotone-notify-hookversion.lua")
--
--    additionally, you can or must define the following variables for
--    use by this software:
--
--        MN_workdir                  MUST be set to the path that can
--                                    be used for work files.
--
--        MN_mail_address_diffs       MAY be set to the mail address to
--                                    send notifications WITH diffs to.
--        MN_mail_address_nodiffs     MAY be set to the mail address to
--                                    send notifications WITHOUT diffs
--                                    to.
--
--                                    NOTE: AT LEAST ONE OF the two
--                                    address variables MUST be defined.
--
--        MN_database                 MUST be set to the database used.
--
--        MN_host                     MAY be set to the address of the
--                                    host to mirror from.
--        MN_collection               MAY be set to the pattern of
--                                    branches to include.  This is
--                                    currently only used for mirroring.
--
--                                    NOTE: IF YOU WANT TO MIRROR, BOTH
--                                    MN_host AND MN_collection MUST BE
--                                    DEFINED.  If one of them isn't,
--                                    both are ignored.
--
--    This software can only be used to send notifications for ALL
--    data received through netsync.  A future version will be able
--    to have the user define several different collection-based
--    criteria and this software will be able to filter according
--    to those criteria.
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
-- Variables
-------------------------------------------------------------------------------

-- True:  script won't be executed, and a message saying its name will
--        be displayed
-- False: script will be executed upon completion
_MN_debug = true

-- A store for the data the belongs to each session.
_MN_sessions = {}

-------------------------------------------------------------------------------
-- Templates for the script that this software creates.
-- NOTE: every line where at least one @var@ thing can be replaced is discarded
-------------------------------------------------------------------------------

_MN_templates = {
   script_start = {
      "#! /bin/sh",
      "",
      "LANG=C; export LANG",
      "mtn --db=@database@ --norc pull @host@ \"@collection@\""
   },

   do_revncert_init = {
      "show_diffs=false",
   },

   do_revision = {
      "mtn --db=@database@ log --last 1 --diffs --from @revision@ > @workdir@/@nonce@.tmp",
      "cat @workdir@/@nonce@.tmp | sed -e '/^============================================================$/,$d' > @workdir@/@nonce@.with-diff.desc",
      "cat @workdir@/@nonce@.tmp | sed -e '/^============================================================$/,$p;d' > @workdir@/@revision@.diff",
      "rm -f @workdir@/@nonce@.tmp",
      "(",
      " cat @workdir@/@nonce@.with-diff.desc",
      " echo",
      " echo 'To get the patch for this revision, please do this:'",
      " echo 'mtn log --last 1 --diffs --from @revision@'",
      ") > @workdir@/@nonce@.without-diff.desc",
      "if [ `grep '^Ancestor: ' @workdir@/@nonce@.with-diff.desc | wc -l` = '1' ]; then show_diffs=true; fi",
   },

   do_revcerts_start = {
      "(echo 'Additional certs:'; echo) >> @workdir@/@nonce@.with-diff.desc"
   },
   do_norevcerts_start = {
      "(echo 'Added certs:'; echo) > @workdir@/@nonce@.with-diff.desc"
   },
   do_cert = {
      "prefix='  @cert_name@'; echo \"@cert_value@\" | while read L; do (echo; echo $prefix : $L) >> @workdir@/@nonce@.with-diff.desc; prefix=`echo \"$prefix\" | sed -e 's/./ /g'`; done"
   },

   do_revncert_end = {
      "if $show_diffs; then",
      "    mime-construct --to @mail_address_diffs@ --subject 'Revision @revision@' --file @workdir@/@nonce@.with-diff.desc --file-attach @workdir@/@revision@.diff",
      "else",
      "    cat @workdir@/@nonce@.without-diff.desc | mailx -s 'Revision @revision@' @mail_address_diffs@",
      "fi",
      "cat @workdir@/@nonce@.without-diff.desc | mailx -s 'Revision @revision@' @mail_address_nodiffs@",
      "rm -f @workdir@/@nonce@.with-diff.desc",
      "rm -f @workdir@/@nonce@.without-diff.desc",
      "rm -f @workdir@/@revision@.diff",
      ""
   },

   do_key_info_init = {
      "(echo 'New public keys:'; echo) > @workdir@/@nonce@.pubkeys"
   },
   do_key_info = {
      "echo \"@keyname@\" >> @workdir@/@nonce@.pubkeys"
   },
   do_key_info_end = {
      "cat @workdir@/@nonce@.pubkeys | mailx -s 'New pubkeys' @mail_address_diffs@",
      "cat @workdir@/@nonce@.pubkeys | mailx -s 'New pubkeys' @mail_address_nodiffs@",
      "rm -f @workdir@/@nonce@.pubkeys",
      ""
   },
   script_end = {
      "rm -f @script@"
   }
}

-------------------------------------------------------------------------------
-- Helper functions
-------------------------------------------------------------------------------

-- These are called at the beginning and end of each hook to provide a trace
function _MN_debug_start(name)
   if _MN_debug then
      io.stderr:write("DEBUG[MN]: ----- start ",name,"\n")
   end
end
function _MN_debug_end(name)
   if _MN_debug then
      io.stderr:write("DEBUG[MN]: ------- end ",name,"\n")
   end
end

-- This is the general function that takes a set of template names and writes
-- all the script lines that were successfully mangled
function _MN_write_script_lines(nonce,items)
   if _MN_sessions[nonce].fd then
      for _, name in pairs(items)
      do
	 for _, line in pairs(_MN_templates[name])
	 do
	    line = string.gsub(line, "%@([_%w]+)%@", _MN_sessions[nonce])
	    if not string.match(line, "%@([_%w]+)%@") then
	       _MN_sessions[nonce].fd:write(line,"\n")
	    end
	 end
      end
   end
end

-- This provides a general check to see if anything needs to be written to
-- the script, in a structured way.  This was needed because we don't know
-- when certain data end, and it would be a pity to create an email for each
-- new pubkey, for example, when one containing all new pubkeys is enough.
function _MN_checks(nonce,rev_id)
   if table.maxn(_MN_sessions[nonce].pubkeys) ~= 0 then
      _MN_write_script_lines(nonce,{"do_key_info_init"})
      for _, keyname in pairs(_MN_sessions[nonce].pubkeys)
      do
	 _MN_sessions[nonce].keyname = keyname
	 _MN_write_script_lines(nonce,{"do_key_info"})
      end
      _MN_write_script_lines(nonce,{"do_key_info_end"})
      _MN_sessions[nonce].pubkeys = {}
   end

   if rev_id ~= _MN_sessions[nonce].revision and
      _MN_sessions[nonce].revision ~= nil then
      local do_end = false
      if _MN_sessions[nonce].new_revision then
	 _MN_write_script_lines(nonce,{"do_revncert_init","do_revision"})
	 cert_start = "do_revcerts_start"
	 do_end = true
      else
	 cert_start = "do_norevcerts_start"
      end
      if table.maxn(_MN_sessions[nonce].certs) ~= 0 then
	 _MN_write_script_lines(nonce,{cert_start})
	 for name, value in pairs(_MN_sessions[nonce].certs)
	 do
	    _MN_sessions[nonce].cert_name = name
	    _MN_sessions[nonce].cert_value = value
	    _MN_write_script_lines(nonce,{"do_cert"})
	 end
	 _MN_write_script_lines(nonce,{"do_revncert_end"})
	 _MN_sessions[nonce].certs = {}
	 do_end = true
      end
      _MN_write_script_lines(nonce,{"do_revncert_end"})
   end

   _MN_sessions[nonce].new_revision = false
   _MN_sessions[nonce].revision = rev_id
end

-------------------------------------------------------------------------------
-- The official hooks
-------------------------------------------------------------------------------

-- Store away the previous implementations, so we can call them first.
_MN_old_note_netsync_start = note_netsync_start
_MN_old_note_netsync_revision_received = note_netsync_revision_received
_MN_old_note_netsync_cert_received = note_netsync_cert_received
_MN_old_note_netsync_pubkey_received = note_netsync_pubkey_received
_MN_old_note_netsync_end = note_netsync_end

function note_netsync_start(nonce)
   _MN_debug_start("MN_note_netsync_start")
   if _MN_old_note_netsync_start then
      _MN_old_note_netsync_start(nonce)
   end
   _MN_sessions[nonce] = {}
   _MN_sessions[nonce].previous_certs = "false"
   _MN_sessions[nonce].nonce = nonce
   _MN_sessions[nonce].workdir = MN_workdir
   _MN_sessions[nonce].mail_address_diffs = MN_mail_address_diffs
   _MN_sessions[nonce].mail_address_nodiffs = MN_mail_address_nodiffs
   _MN_sessions[nonce].database = MN_database
   _MN_sessions[nonce].host = MN_host
   _MN_sessions[nonce].collection = MN_collection
   if not (MN_workdir and MN_database) then
      io.stderr:write("Error: the variables MN_workdir and MN_database\n")
      io.stderr:write("Error: are not set!\n")
      io.stderr:write("There will be no script and therefore no mail\n")
   else
      _MN_sessions[nonce].script = _MN_sessions[nonce].workdir .. "/" .. nonce .. ".sh"
      _MN_sessions[nonce].fd,message = io.open(_MN_sessions[nonce].script,"w")
      if _MN_sessions[nonce].fd == nil then
	 io.stderr:write("Error: Couldn't create file ".._MN_sessions[nonce].script.."\n")
	 io.stderr:write("There will be no script and therefore no mail\n")
      end
   end
   if not (MN_mail_address_diffs or MN_mail_address_nodiffs) then
      io.stderr:write("Warning: the variables MN_mail_address_diffs and\n")
      io.stderr:write("Warning: MN_mail_address_nodiffs are not set!\n")
      io.stderr:write("There will be a script, but it won't mail anywhere!\n")
   end
   _MN_sessions[nonce].revision = nil
   _MN_sessions[nonce].new_revision = false
   _MN_sessions[nonce].pubkeys = {}
   _MN_sessions[nonce].certs = {}
   _MN_write_script_lines(nonce,{"script_start"})
   _MN_debug_end("MN_note_netsync_start")
end

function note_netsync_revision_received(new_id,revision,certs,nonce)
   _MN_debug_start("MN_note_netsync_revision_received\n  "..new_id)
   if _MN_old_note_netsync_revision_received then
      _MN_old_note_netsync_revision_received(new_id,revision,certs,nonce)
   end
   _MN_checks(nonce,new_id)
   _MN_sessions[nonce].new_revision = true
   _MN_sessions[nonce].certs = {}
   for _, item in pairs(certs)
   do
      if item.name ~= "author" and 
	 item.name ~= "branch" and
	 item.name ~= "changelog" and
	 item.name ~= "date" and
	 item.name ~= "tag" then
	 if _MN_debug then
	    io.stderr:write("DEBUG[MN]: --- add cert\n  "..new_id.."\n  "..name.."\n  "..value.."\n")
	 end
	 _MN_sessions[nonce].certs[item.name] = item.value
      end
   end
   _MN_debug_end("MN_note_netsync_revision_received\n  "..new_id)
end

function note_netsync_cert_received(rev_id,key,name,value,nonce)
   _MN_debug_start("MN_note_netsync_cert_received\n  "..rev_id.."\n  "..name.."\n  "..value)
   if _MN_old_note_netsync_cert_received then
      _MN_old_note_netsync_cert_received(rev_id,key,name,value,nonce)
   end
   _MN_checks(nonce,rev_id)
   if _MN_debug then
      io.stderr:write("DEBUG[MN]: --- add cert\n  "..rev_id.."\n  "..name.."\n  "..value.."\n")
   end
   _MN_sessions[nonce].certs[name] = value
   _MN_debug_end("MN_note_netsync_cert_received\n  "..rev_id)
end

function note_netsync_pubkey_received(keyname,nonce)
   _MN_debug_start("MN_note_netsync_pubkey_received\n  "..keyname)
   if _MN_old_note_netsync_pubkey_received then
      _MN_old_note_netsync_pubkey_received(keyname,nonce)
   end
   table.insert(_MN_sessions[nonce].pubkeys,keyname)
   _MN_debug_end("MN_note_netsync_pubkey_received\n  "..keyname)
end

function note_netsync_end(nonce)
   _MN_debug_start("MN_note_netsync_end")
   if _MN_old_note_netsync_end then
      _MN_old_note_netsync_end(nonce)
   end
   _MN_checks(nonce,nil)
   _MN_write_script_lines(nonce,{"script_end"})
   if _MN_sessions[nonce].fd then
      io.close(_MN_sessions[nonce].fd)
      if _MN_debug then
	 io.stderr:write("DEBUG[MN]: wrote script ",_MN_sessions[nonce].script,
			 "\n")
      else
	 spawn("sh",_MN_sessions[nonce].script)
      end
   end
   _MN_sessions[nonce] = nil
   _MN_debug_end("MN_note_netsync_end")
end
