
mtn_setup()

check(mtn("pull", "nosuchhost__blahblah__asdvasoih.com", "some.pattern"), 1, false, false)
