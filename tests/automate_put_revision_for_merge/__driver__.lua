-- test that 'automate put_revision' works for merge revisions
mtn_setup()

addfile("foo", "blah blah parent")
commit()
f_parent = sha1("foo")
r_parent = base_revision()

writefile("foo", "blah blah left")
commit()
f_left = sha1("foo")
r_left = base_revision()

revert_to(r_parent)

writefile("foo", "blah blah right")
commit()
f_right = sha1("foo")
r_right = base_revision()

writefile("foo-merge", "blah blah merge")
f_merge = sha1("foo-merge")

-- intentionally somewhat idiosyncratic whitespace...
revision_text = ("format_version \"1\"\n"
                 .. "new_manifest [aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa]\n"
                 .. "old_revision [" .. r_left .. "]\n"
                 .. "patch \"foo\" from [" .. f_left .. "] \n"
                 .. "to [" .. f_merge .. "]\n"
                 .. "\n"
                 .. "old_revision [" .. r_right .. "]\n"
                 .. "patch \"foo\" from [" .. f_right .. "] \n"
                 .. "to [" .. f_merge .. "]\n"
                 .. "\n"
                 .. "\n\n\n" -- just for fun
              )
check(mtn("automate", "put_file", readfile("foo-merge")), 0, false, false)
check(mtn("automate", "put_revision", revision_text), 0, true, false)
r_merge = trim(readfile("stdout"))

check(mtn("update", "-r", r_merge, "-b", "asdf"), 0, false, false)
check(base_revision() == r_merge)
check(samefile("foo", "foo-merge"))
