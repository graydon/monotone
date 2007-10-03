-- The following hooks are run as soon as revisions or certs to existing revisions
-- are arriving via netsync. Then they note if a particular, pre-defined branch
-- (_branch) is touched and if so, a script (_updater) is run at the end of the
-- netsync session.
--
-- A sample script which updates a particular workspace automatically could look
-- like this. This is particularily useful if you manage a website with monotone
-- and want that your just committed changes popup on the web server: 
--
-- #!/bin/bash
-- branch="my.target.branch"
-- workspace="/path/to/workspace"
-- cd $workspace
-- # only update if there is no divergency
-- heads=`mtn heads -b $branch 2>/dev/null | wc -l`
-- if [ "$heads" != "1" ]; then exit; fi
-- mtn up -r "h:$branch" >/dev/null 2>&1
--
-- Copy the following lua hooks into your monotonerc file or load them with 
-- --rcfile for the monotone process which serves your database.
--
-- License: GPL 
--
-- Version history:
-- ----------------
-- 
-- 0.1 (2007-01-29) Thommas Keller <me@thomaskeller.biz>
--     - initial version
--

_branch = "my.target.branch"
_updater = "/path/to/update.sh"
_sessions = {}

-- fixme: only session_id is set, so we can't check the server's role or sync type here!
-- this seems to be some weird bug with monotone (tested with 0.31)
function note_netsync_start (session_id, my_role, sync_type, remote_host, remote_keyname, includes, excludes)
        print("netsync_start: starting netsync communication")
        _sessions[session_id] = 0
end

function note_netsync_revision_received (new_id, revision, certs, session_id)
        if _sessions[session_id] == nil then
                print("revision_received: no session present")
                return
        end

        for i,cert in ipairs(certs) do
                if cert["name"] == "branch" and cert["value"] == _branch then
                        print("revision_received: found another interesting revision")
                        _sessions[session_id] = _sessions[session_id] + 1
                end
        end
end

-- check if an interesting cert has arrived due to propagate
function note_netsync_cert_received (rev_id, key, name, value, session_id)
        if _sessions[session_id] == nil then
                print("cert_received: no session present")
                return
        end

        if name == branch and value == _branch then
                print("cert_received: found another interesting cert")
                _sessions[session_id] = _sessions[session_id] + 1
        end
end

function note_netsync_end (session_id, status, bytes_in, bytes_out, certs_in, certs_out, revs_in, revs_out, keys_in, keys_out)
        if _sessions[session_id] == nil then
                print("netsync_end: no session present")
                return
        end

        -- fixme: we should check status for being != 200, but as above, it seems as this is not set

        -- if no interesting revisions arrived, skip the update
        if _sessions[session_id] == 0 then
                print("netsync_end: no interesting revisions/certs received")
                return
        end

        _sessions[session_id] = nil

        print("netsync_end: running update script")
        spawn(_updater)
end
