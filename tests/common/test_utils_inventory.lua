-- Functions useful in tests/automate_inventory*

function checkexp (label, computed, expected, xfail)
-- Throw an error with a helpful message if 'computed' doesn't equal
-- 'expected'.
   if computed ~= expected then
      if xfail then
         err(false, 2)
      else
         err (label .. " Expected '" .. expected .. "' got '" .. computed .. "'")
      end
   end
end

function check_basic_io_line (label, computed, name, value, xfail)
-- Compare a parsed basic_io line 'computed' to 'name', 'value', throw
-- an error (with a helpful message) if they don't match.
   checkexp(label .. ".name", computed.name, name, xfail)

   if type(value) == "table" then
      checkexp(label .. ".length", #computed.values, #value, xfail)
      for i = 1, #value do
         checkexp(label .. i, computed.values[i], value[i], xfail)
      end

   else
      checkexp(label .. ".length", #computed.values, 1, xfail)
      checkexp(label .. "." .. name, computed.values[1], value, xfail)
   end
end

function find_basic_io_line (parsed, line)
-- return index in parsed (output of parse_basic_io) matching
-- line.name, line.values
   for I = 1, #parsed do
       if parsed[I].name == line.name then
          if type (line.values) ~= "table" then
             if parsed[I].values[1] == line.values then
                return I
             end
          else
             err ("searching for line with table of values not yet supported")
          end
       end
   end

   err ("line '" .. line.name .. " " .. line.values .. "' not found")
end

function xfail_inventory (parsed, parsed_index, stanza)
   return check_inventory(parsed, parsed_index, stanza, true)
end -- check_inventory

function check_inventory (parsed, parsed_index, stanza, xfail)
-- 'stanza' is a table for one stanza
-- 'parsed_index' gives the first index for this stanza in 'parsed'
-- (which should be the output of parse_basic_io).
-- Returns parsed_index incremented to the next index to check.

   -- we assume that any test failure is not an expected failure if not
   -- otherwise given
   if xfail == nil then xfail = false end

   check_basic_io_line (parsed_index, parsed[parsed_index], "path", stanza.path, xfail)
   parsed_index = parsed_index + 1

   if stanza.old_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_type", stanza.old_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.new_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_path", stanza.new_path, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.new_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_type", stanza.new_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.old_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_path", stanza.old_path, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.fs_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "fs_type", stanza.fs_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.birth then
      check_basic_io_line (parsed_index, parsed[parsed_index], "birth", stanza.birth, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.status then
      check_basic_io_line (parsed_index, parsed[parsed_index], "status", stanza.status, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.changes then
      check_basic_io_line (parsed_index, parsed[parsed_index], "changes", stanza.changes, xfail)
      parsed_index = parsed_index + 1
   end

   return parsed_index
end -- check_inventory

-- end of file
