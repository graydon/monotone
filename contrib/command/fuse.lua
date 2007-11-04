--
--  fuse.lua -- Monotone Lua extension command "mtn fuse"
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
    "fuse", "REVISION",
    "Fuse revision into workspace with conflict markers.",
    "Fuse the specified revision into the current workspace by merging " ..
    "the revision into the workspace while providing inline conflict " ..
    "markers for manually resolving the conflicts in the workspace " ..
    "before comitting the conflicts-resolved workspace as the new " ..
    "merged revision.",
    "command_fuse"
)

function command_fuse(revision)
    --  argument sanity checking
    if revision == nil then
        io.stderr:write("mtn: fuse: ERROR: revision not given\n")
        return
    end

    --  run-time sanity checking
    if program_exists_in_path("mtn") == 0 then
        io.stderr:write("mtn: fuse: ERROR: require Monotone command \"mtn\" in PATH\n")
        return
    end

    --  make sure we have a valid workspace
    mtn_automate("get_option", "branch")

    --  perform the revision "fusion" operation
    local cmd =
        "MTN_MERGE=diffutils " ..
        "MTN_MERGE_DIFFUTILS=\"partial,diff3opts=-E\" " ..
        "mtn " .. "merge_into_workspace " .. revision 
    local rc = execute("sh", "-c", cmd)
    if rc ~= 0 then
        io.stderr:write("mtn: fuse: ERROR: failed to execute command \"" .. cmd .. "\"\n")
    end
end

