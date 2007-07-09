
mtn_setup()

-- No completion
check(mtn("automate", "leaves"), 0, false, false)

-- Complete subcommand
check(mtn("automate", "lea"), 1, false, false)

-- Complete "automate"
check(mtn("automat", "leaves"), 1, false, false)

-- Alias for people who like using automate manually
check(mtn("au", "leaves"), 0, false, false)
