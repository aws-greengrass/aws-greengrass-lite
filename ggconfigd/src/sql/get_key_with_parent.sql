SELECT
  kt.keyid
FROM
  keyTable kt
  LEFT JOIN relationTable rt
WHERE
  kt.keyid = rt.keyid
  AND kt.keyvalue = ?
  AND rt.parentid = ?;
