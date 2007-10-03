
mtn_setup()

check(mtn("crash", "I"), 3, false, false)
check(exists("_MTN/debug"))

check(remove("_MTN"))
check(mtn("crash", "I"), 3, false, false)
check(exists("dump"))

mkdir("try")
check(mtn("crash", "I", "--confdir=try"), 3, false, false)
check(exists("try/dump"))

check(mtn("crash", "I", "--dump=fork"), 3, false, false)
check(exists("fork"))

-- all the exceptions caught in monotone.cc and translated to error messages
for _,tag in pairs({  'std::bad_cast',
		      'std::bad_typeid',
		      'std::bad_exception',
		      'std::domain_error',
		      'std::invalid_argument',
		      'std::length_error',
		      'std::out_of_range',
		      'std::range_error',
		      'std::overflow_error',
		      'std::underflow_error',
		      'std::logic_error',
		      'std::runtime_error',
		      'std::exception' }) do
   remove("fork")
   check(mtn("crash", tag, "--dump=fork"), 3, false, false)
   check(exists("fork"))
end

-- bad_alloc is special
remove("fork")
check(mtn("crash", "std::bad_alloc", "--dump=fork"), 1, false, false)
check(not exists("fork"))

-- selected signals - note hardwired signal numbers :(
skip_if(ostype == "Windows")
remove("fork")
for _,tag in pairs({ 3, 6, 11 }) do
   check(mtn("crash", tag, "--dump=fork"), -tag, false, false)
   check(not exists("fork"))
end
