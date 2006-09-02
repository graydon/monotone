if (attr_init_functions == nil) then
  attr_init_functions = {}
end
attr_init_functions["test:test_attr"] =
  function(filename)
     if filename == "magic" then
        return "bob"
     else
        return nil
     end
  end
