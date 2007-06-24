
mtn_setup()

mkdir("foo")
mkdir("foo/keys")

check(raw_mtn("ls", "keys", "--confdir=foo", "--keydir=foo/keys"), 0, true, false)

copy("stdout", "good")

check(raw_mtn("ls", "keys", "--confdir=foo"), 0, {"good"}, false)

check(raw_mtn("ls", "keys", "--keydir=foo/keys", "--confdir=."), 0, {"good"}, false)

check(raw_mtn("ls", "keys", "--confdir=.", "--keydir=foo/keys"), 0, {"good"}, false)
