skip_if(not existsonpath("cvs"))

function cvs(...)
  return {"cvs", "-d", cvsroot, unpack(arg)}
end

function cvs_setup()
  cvsroot = test.root .. "/cvs-repository"
  check(cvs("-q", "init"), 0, false, false)
  check(exists(cvsroot))
  check(exists(cvsroot .. "/CVSROOT"))
  check(exists(cvsroot .. "/CVSROOT/modules"))
end
