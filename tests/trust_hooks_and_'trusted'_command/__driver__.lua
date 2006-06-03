
mtn_setup()

function trusted(rev, name, value, ...) -- ... is signers
  check(mtn("trusted", rev, name, value, unpack(arg)), 0, true, false)
  local t = qgrep(" trusted", "stdout")
  local u = qgrep(" untrusted", "stdout") or qgrep(" UNtrusted", "stdout")
  check(t ~= u)
  return t
end

good = string.rep("1", 40)
bad = string.rep("0", 40)

-- Idea here is to check a bunch of combinations, to make sure that
-- trust hooks get all information correctly
check(trusted(good, "foo", "bar", "foo@bar.com"))
check(trusted(good, "foo", "bar", "alice@trusted.com"))
check(not trusted(good, "foo", "bar", "mallory@evil.com"))
check(trusted(good, "bad-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "good-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "bad-cert", "good-val", "foo@bar.com"))
check(not trusted(bad, "bad-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "bad-cert", "bad-val", "alice@trusted.com"))

check(trusted(good, "foo", "bar", "foo@bar.com", "alice@trusted.com"))
check(trusted(good, "foo", "bar", "alice@trusted.com", "foo@bar.com"))
check(not trusted(good, "foo", "bar", "foo@bar.com", "mallory@evil.com"))
check(not trusted(good, "foo", "bar", "mallory@evil.com", "foo@bar.com"))
check(trusted(bad, "bad-cert", "bad-val", "foo@bar.com", "alice@trusted.com"))
check(trusted(bad, "bad-cert", "bad-val", "alice@trusted.com", "foo@bar.com"))
