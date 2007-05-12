-- Functions useful in tests/automate_inventory*

function checkexp (label, computed, expected)
-- Throw an error with a helpful message if 'computed' doesn't equal
-- 'expected'.
   if computed ~= expected then
      err (label .. " Expected '" .. expected .. "' got '" .. computed .. "'")
   end
end

function check_basic_io_line (label, computed, name, value)
-- Compare a parsed basic_io line 'computed' to 'name', 'value', throw
-- an error (with a helpful message) if they don't match.
   checkexp(label .. ".name", computed.name, name)

   if type(value) == "table" then
      checkexp(label .. ".length", #computed.values, #value)
      for i = 1, #value do
         checkexp(label .. i, computed.values[i], value[i])
      end

   else
      checkexp(label .. ".length", #computed.values, 1)
      checkexp(label .. "." .. name, computed.values[1], value)
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

function check_inventory (parsed, parsed_index, stanza)
-- 'stanza' is a table for one stanza
-- 'parsed_index' gives the first index for this stanza in 'parsed'
-- (which should be the output of parse_basic_io).
-- Returns parsed_index incremented to the next index to check.

   check_basic_io_line (parsed_index, parsed[parsed_index], "path", stanza.path)
   parsed_index = parsed_index + 1

   if stanza.old_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_type", stanza.old_type)
      parsed_index = parsed_index + 1
   end

   if stanza.new_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_path", stanza.new_path)
      parsed_index = parsed_index + 1
   end

   if stanza.new_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_type", stanza.new_type)
      parsed_index = parsed_index + 1
   end

   if stanza.old_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_path", stanza.old_path)
      parsed_index = parsed_index + 1
   end

   if stanza.fs_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "fs_type", stanza.fs_type)
      parsed_index = parsed_index + 1
   end

   if stanza.status then
      check_basic_io_line (parsed_index, parsed[parsed_index], "status", stanza.status)
      parsed_index = parsed_index + 1
   end

   if stanza.changes then
      check_basic_io_line (parsed_index, parsed[parsed_index], "changes", stanza.changes)
      parsed_index = parsed_index + 1
   end

   return parsed_index
end -- check_inventory

-- end of file
