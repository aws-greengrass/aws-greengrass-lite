SELECT
  k.keyId,
  k.keyvalue
FROM
  relationTable r
  INNER JOIN keyTable k ON r.keyId = k.keyId
WHERE
  r.parentid = ?;
