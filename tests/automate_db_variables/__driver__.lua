
mtn_setup()

--
-- check set_db_variable
--

check(mtn("automate", "set_db_variable", "domain1", "var", "value"), 0, false, false);
check(mtn("automate", "set_db_variable", "domain2", "var", "value"), 0, false, false);
-- too many arguments
check(mtn("automate", "set_db_variable", "domain1", "var", "new_value", "junk"), 1, false, false);
-- too few arguments
check(mtn("automate", "set_db_variable", "domain1", "other_var"), 1, false, false);



--
-- check get_db_variables
--

writefile("expected1",
          'domain "domain1"\n' ..
          ' entry "other_var" "value"\n' ..
          ' entry "var" "value"\n\n' ..
          'domain "domain2"\n' ..
          ' entry "var" "value"\n')

writefile("expected2",
          'domain "domain2"\n' ..
          ' entry "var" "value"\n')

check(mtn("automate", "set_db_variable", "domain1", "other_var", "value"), 0, false, false);

check(mtn("automate", "get_db_variables"), 0, true, false)
check(samefile("expected1", "stdout"))

check(mtn("automate", "get_db_variables", "domain2"), 0, true, false)
check(samefile("expected2", "stdout"))

check(mtn("automate", "get_db_variables", "unknown_domain"), 1, false, false)


--
-- check drop_db_variables
--

check(mtn("automate", "drop_db_variables", "domain1", "var"), 0, false, false);
-- already dropped
check(mtn("automate", "drop_db_variables", "domain1", "var"), 1, false, false);
-- drops vars from domain2
check(mtn("automate", "drop_db_variables", "domain2"), 0, false, false);
-- otherwise unknown domain
check(mtn("automate", "drop_db_variables", "domain2"), 1, false, false);

