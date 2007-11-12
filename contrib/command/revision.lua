--
--  revision.lua -- Monotone Lua extension command "mtn revision"
--  Copyright (C) 2007 Ralf S. Engelschall <rse@engelschall.com>
--
--  This program is made available under the GNU GPL version 2.0 or
--  greater. See the accompanying file COPYING for details.
--
--  This program is distributed WITHOUT ANY WARRANTY; without even the
--  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
--  PURPOSE.
--

register_command(
    "revision", "REVISION [ANCESTOR-REVISION]",
    "Shows summary information about revision(s)",
    "Shows summary information about a particular revision " ..
    "(or a range of revisions in case an ancestor revision is also specified). " ..
    "This is just a convenience wrapper command around \"mtn log --diffs\".",
    "command_revision"
)

alias_command(
    "revision",
    "rev"
)

function command_revision(revision, ancestor)
    --  argument sanity checking
    if revision == nil then
        io.stderr:write("mtn: revision: ERROR: no revision specified\n")
        return
    end
    if ancestor == nil then
        ancestor = revision
    end

    --  make sure we have a valid workspace
    mtn_automate("get_option", "branch")

    --  perform the operation
    execute("mtn", "log", "--diffs", "--no-graph", "--from", ancestor, "--to", revision)
    if rc ~= 0 then
        io.stderr:write("mtn: revision: ERROR: failed to execute\n")
    end
end

