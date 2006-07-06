
mtn_setup()

european_utf8 = "\195\182\195\164\195\188\195\159"
             -- "\xC3\xB6\xC3\xA4\xC3\xBc\xC3\x9F"
european_8859_1 = "\246\228\252\223"
               -- "\xF6\xE4\xFC\xDF"
japanese_utf8 = "\227\129\166\227\129\153\227\129\168"
             -- "\xE3\x81\xA6\xE3\x81\x99\xE3\x81\xA8"
japanese_euc_jp = "\164\198\164\185\164\200"
               -- "\xA4\xC6\xA4\xB9\xA4\xC8"

set_env("CHARSET", "UTF-8")
addfile("a", "hello there")
check(mtn("--debug", "commit", "--message", european_utf8), 0, false, false)


set_env("CHARSET", "iso-8859-1")
addfile("b", "hello there")
check(mtn("--debug", "commit", "--message", european_8859_1), 0, false, false)


set_env("CHARSET", "UTF-8")
addfile("c", "hello there")
check(mtn("--debug", "commit", "--message", japanese_utf8), 0, false, false)


set_env("CHARSET", "euc-jp")
addfile("d", "hello there")
check(mtn("--debug", "commit", "--message", japanese_euc_jp), 0, false, false)
