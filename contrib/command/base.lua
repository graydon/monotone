--
--  base.lua -- Monotone Lua extension command "mtn base"
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
    "base", "upgrade|diff",
    "Upgrades or compares current branch against base branch",
    "Upgrade current branch from base branch or compares current " ..
    "branch against base branch. The base branch has to be stored " ..
    "either in the \"mtn:base\" attribute of the root directory " ..
    "or in a \".mtn-base\" file in the root directory.",
    "command_base"
)

function command_base(op)
    --  sanity check command line
    if op == nil then
        io.stderr:write("mtn: base: ERROR: no operation specified\n")
        return
    end
    if op ~= "upgrade" and op ~= "diff" then
        io.stderr:write("mtn: base: ERROR: either \"upgrade\" or \"diff\" operation has to be specified\n")
        return
    end

    --  determine current branch of workspace
    local branch_this = nil
    local rc, txt = mtn_automate("get_option", "branch")
    if txt ~= nil then
        branch_this = string.match(txt, "^%s*(%S+)%s*$")
    end
    if branch_this == nil then
        io.stderr:write("mtn: base: ERROR: failed to determine current branch\n")
        return
    end

    --  determine base branch of workspace
    local branch_base = nil
    local rc, txt = mtn_automate("get_attributes", ".")
    if txt ~= nil then
        branch_base = string.match(txt, "attr%s+\"mtn:base\"%s+\"([^\"]+)\"")
    end
    if branch_base == nil then
        local txt = read_contents_of_file(".mtn-base", "r")
        if txt ~= nil then
            branch_base = string.match(txt, "^%s*(%S+)%s*$")
        end
    end
    if branch_base == nil then
        io.stderr:write("mtn: base: ERROR: failed to determine base branch\n")
        return
    end

    --  dispatch according to operation
    if op == "upgrade" then
        --  upgrade current branch by merging in revisions of base branch
        local rc = execute("mtn", "propagate", branch_base, branch_this)
        if rc ~= 0 then
            io.stderr:write("mtn: base: ERROR: failed to execute \"mtn propagate\"\n")
            return
        end
        rc = execute("mtn", "update")
        if rc ~= 0 then
            io.stderr:write("mtn: base: ERROR: failed to execute \"mtn update\"\n")
            return
        end
    elseif op == "diff" then
        --  upgrade current branch by merging in revisions of base branch
        local rc = execute("mtn", "diff", "-r", "h:" .. branch_base, "-r", "h:" .. branch_this)
        if rc ~= 0 then
            io.stderr:write("mtn: base: ERROR: failed to execute \"mtn diff\"\n")
            return
        end
    end
    return
end

