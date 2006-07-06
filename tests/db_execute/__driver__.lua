
mtn_setup()

two_col_table = "files"
col1 = "id"

function dbex(...)
  check(mtn("db", "execute", string.format(unpack(arg))), 0, true, false)
end

dbex("INSERT INTO %s VALUES ('key1', 'value1')", two_col_table)

dbex("SELECT * FROM %s", two_col_table)
check(qgrep("key1", "stdout"))
check(qgrep("value1", "stdout"))

dbex("SELECT * FROM %s WHERE %s = 'nonsense'", two_col_table, col1)
check(not qgrep("key1", "stdout"))

dbex("SELECT * FROM %s WHERE %s LIKE 'k%%'", two_col_table, col1)
check(qgrep("key1", "stdout"))

dbex("DELETE FROM %s", two_col_table)

dbex("SELECT * FROM %s", two_col_table)
check(not qgrep("key1", "stdout"))

-- We used to have weird quoting bugs around "%"
-- string is split into two so the grep doesn't trigger on monotone's
-- chatter about what command it's going to execute...
dbex("SELECT '%%s' || 'tuff'")
check(qgrep('%stuff', "stdout"))
