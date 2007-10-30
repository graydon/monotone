--
--  conflicts.lua -- Monotone Lua extension command "mtn conflicts"
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
    "conflicts", "",
    "Lists files in workspace containing conflict markers.",
    "Lists all files in the current workspace containing the " ..
    "conflict markers produced by GNU diffutils' \"diff3\" " ..
    "command. This indicates still unresolved merge conflicts.",
    "command_conflicts"
)

function command_conflicts()
    --  sanity check run-time environment
    if program_exists_in_path("egrep") == 0 then
        io.stderr:write("mtn: conflicts: ERROR: require GNU grep command \"egrep\" in PATH\n")
        return
    end

    --  make sure we have a valid workspace
    mtn_automate("get_option", "branch")

    --  perform check operation via GNU grep's egrep(1)
    local rc = execute(
        "egrep",
        "--files-with-matches",
        "--recursive",
        "^(<<<<<<<|=======|>>>>>>>) ",
        "."
    )
end

