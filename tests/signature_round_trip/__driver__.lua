mtn_setup()

-- create a revision to apply our fake cert to
addfile("testfile", "a file\n")
commit()
rev = base_revision()

-- apply a cert to that revision
check(mtn("cert", rev, "signature-check", "test string to sign"),
      0, false, false)

-- check it
value, goodsig = certvalue(rev, "signature-check")
check(value == "test string to sign")
check(goodsig == "ok")

-- change the cert value with raw database ops
check(mtn("db", "execute",
	  'update revision_certs' ..
          '    set value = "test string to sign ...with a lie"' ..
          '  where name = "signature-check"'),
      0, false, false)

-- certvalue should now report a bad signature
value, goodsig = certvalue(rev, "signature-check")
check(value == "test string to sign ...with a lie")
check(goodsig == "bad")
