DELETE FROM subscriberTable
WHERE
  keyid NOT IN (
    SELECT
      keyid
    FROM
      keyTable
  )
