
mtn_setup()

-- This test imports a rcs file which fails according to the mailing list
-- reporter: Carl Christian Kanne "Bug? in CVS import monotone 0.18"
-- Date: 	Fri, 15 Apr 2005 12:53:13 +0200

-- This test is a bug report.

-- This rcs file fails to be imported correctly by monotone
check(get("cvs-repository"))

xfail_if(true, mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, ignore, ignore)
