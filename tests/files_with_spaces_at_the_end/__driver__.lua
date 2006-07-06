
mtn_setup()
skip_if(ostype == "Windows")
-- On Win32, the files "foo bar" and "foo bar " are the same, obviating this test

writefile("foo bar ")
check(mtn("add", "foo bar "), 0, false, false)
commit()
