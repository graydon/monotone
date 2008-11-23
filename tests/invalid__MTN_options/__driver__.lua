mtn_setup()

check(mtn("st"), 0, false, false)

writefile("_MTN/options", 'database "test.db"\nbranch "x"\n')
check(mtn("st"), 0, false, false)

writefile("_MTN/options", 'database "test.db\nbranch "x"\n')
check(mtn("st"), 1, false, false)

writefile("_MTN/options", 'database "test.db\nbranch "0"\n')
check(mtn("st"), 1, false, false)
