
mtn_setup()

-- Completion of a command.
check(mtn("status"), 0, false, false)
check(mtn("st"), 0, false, false)
check(mtn("s"), 1, false, false)

-- Completion of a command.
check(mtn("diff"), 0, false, false)
check(mtn("dif"), 0, false, false)
check(mtn("di"), 1, false, false)

-- Completion of a subcommand at the second level.
check(mtn("list", "key"), 0, false, false)
check(mtn("list", "ke"), 0, false, false)
check(mtn("list", "k"), 1, false, false)

-- Completion of a subcommand at the first level.
check(mtn("list", "keys"), 0, false, false)
check(mtn("lis", "keys"), 0, false, false)
check(mtn("li", "keys"), 0, false, false)
check(mtn("l", "keys"), 0, false, false)

-- Completion of a subcommand at the two levels.
check(mtn("list", "keys"), 0, false, false)
check(mtn("lis", "key"), 0, false, false)
check(mtn("li", "ke"), 0, false, false)
check(mtn("l", "k"), 1, false, false)
