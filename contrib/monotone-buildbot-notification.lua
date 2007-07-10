-- The following hook informs the buildbot about a new revision received
-- via netsync. This is done via the `buildbot sendchange` command here and
-- with the PBChangeSource on the buildbot server side.
-- 
--
-- Version history:
-- ----------------
-- 
-- 0.1 (2007-07-10) Markus Schiltknecht <markus@bluegap.ch>
--     - initial version
--
-- License: GPL
--

_buildbot_bin = "/usr/bin/buildbot"
_buildbot_addr = "localhost:9989"

function notify_buildbot(rev_id, revision, certs)
    local author = ""
    local changelog = ""
    local branch = ""
    for i,cert in pairs(certs) do 
        if cert["name"] == "changelog" then
            changelog = changelog .. cert["value"] .. "\n"
	elseif cert["name"] == "author" then
	    -- we simply override the author, in case there are multiple
	    -- author certs.
	    author = cert["value"]
	elseif cert["name"] == "branch" then
	    -- likewise with the branch cert, which probably isn't that
	    -- clever...
	    branch = cert["value"]
	end
    end

    local touched_files = ""
    for i,row in ipairs(parse_basic_io(revision)) do
        local key = row["name"]
	if (key == 'delete') or (key == 'add_dir') or (key == 'add_file') or
	   (key == 'patch') then
	    local filename = row["values"][1]
	    touched_files = touched_files .. filename .. " "
	end
    end

    execute(_buildbot_bin, "sendchange",
	    "--master", _buildbot_addr,
	    "--username", author,
	    "--revision", rev_id,
	    "--comments", changelog,
	    "--branch", branch,
	    touched_files)
end

function note_commit (new_id, revision, certs)
    notify_buildbot(new_id, revision, certs)
end

function note_netsync_revision_received(new_id, revision, certs, session_id)
    notify_buildbot(new_id, revision, certs)
end
