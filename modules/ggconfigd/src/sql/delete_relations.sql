DELETE FROM relationTable
WHERE
  keyid = ?
  OR parentid = ?;
