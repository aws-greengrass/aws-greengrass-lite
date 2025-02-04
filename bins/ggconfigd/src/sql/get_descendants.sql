WITH RECURSIVE
  descendants (keyid) AS (
    SELECT
      keyid
    FROM
      relationTable
    WHERE
      parentid = ?
    UNION ALL
    SELECT
      r.keyid
    FROM
      relationTable r
      JOIN descendants d ON r.parentid = d.keyid
  )
SELECT
  keyid
FROM
  descendants
UNION
SELECT
  ?;
