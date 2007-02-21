mtn_setup()

edge = "format_version \"1\"\n\nnew_manifest [0000000000000000000000000000000000000004]\n\nold_revision []\n\nadd_dir \"\"\n\nadd_file \"foo\"\n content [5bf1fd927dfb8679496a2e6cf00cbe50c1c87145]\n"

check(mtn("automate", "put_file", "blah"), 0, true, false)
canonicalize("stdout")
file = "5bf1fd927dfb8679496a2e6cf00cbe50c1c87145"
result = readfile("stdout")
check(result == file.."\n")

check(mtn("automate", "put_revision", edge), 0, true, false)
canonicalize("stdout")
rev = "4c2c1d846fa561601254200918fba1fd71e6795d"
result = readfile("stdout")
check(result == rev.."\n")

check(mtn("automate", "cert", rev, "author", "tester@test.net"), 0, true, false)
check(mtn("automate", "cert", rev, "branch", "testbranch"), 0, true, false)
check(mtn("automate", "cert", rev, "changelog", "blah-blah"), 0, true, false)
check(mtn("automate", "cert", rev, "date", "2005-05-21T12:30:51"), 0, true, false)

check(mtn("automate", "heads", "testbranch"), 0, true, false)
canonicalize("stdout")
check(rev.."\n" == readfile("stdout"))
