SELECT
  keyid
FROM
  keyTable
WHERE
  keyid NOT IN (
    SELECT
      keyid
    FROM
      relationTable
  )
  AND keyvalue = ?;
