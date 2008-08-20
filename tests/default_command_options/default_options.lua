
function get_default_command_options(command)
    local default_opts = {}
    if (command[1] == "version") then
        table.insert(default_opts, "--full")
    end
    -- should trigger an invalid option error
    if (command[1] == "status") then
        table.insert(default_opts, "--foobarbaz")
    end
    return default_opts
end
