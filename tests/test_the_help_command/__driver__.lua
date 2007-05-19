
mtn_setup()

-- The 'help' command behaves exactly as the '--help' option.
check(mtn("help", "mv"), 0, true, 0)
rename("stdout", "out")
check(mtn("--help", "mv"), 0, true, 0)
check(samefile("stdout", "out"))

-- Help on a top-level group.
check(mtn("help", "review"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Commands in group 'review'") ~= nil)

-- Help on a top-level group; completion not supported.
check(mtn("help", "revie"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "unknown command") ~= nil)

-- Help on a command group.
check(mtn("help", "list"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Subcommands of 'mtn list'") ~= nil)
check(string.find(output, "Description for 'mtn list'") ~= nil)
check(string.find(output, "Aliases: ls.") ~= nil)

-- Help on a command group using one of its aliases.
check(mtn("help", "ls"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Subcommands of 'mtn ls'") ~= nil)
check(string.find(output, "Description for 'mtn ls'") ~= nil)
check(string.find(output, "Aliases: list.") ~= nil)

-- Help on a command with additional options.
check(mtn("help", "checkout"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Options specific to 'mtn checkout'") ~= nil)
check(string.find(output, "Description for 'mtn checkout'") ~= nil)
check(string.find(output, "--branch") ~= nil)

-- Help on a subcommand.
check(mtn("help", "list", "keys"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Syntax specific to 'mtn list keys'") ~= nil)
check(string.find(output, "Description for 'mtn list keys'") ~= nil)

-- Help on a subcommand using an alias for the intermediate name.
check(mtn("help", "ls", "keys"), 0, true, "")
output = readfile("stdout")
check(string.find(output, "Syntax specific to 'mtn ls keys'") ~= nil)
check(string.find(output, "Description for 'mtn ls keys'") ~= nil)
