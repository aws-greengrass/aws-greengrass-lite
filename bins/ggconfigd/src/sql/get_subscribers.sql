SELECT
  handle
FROM
  subscriberTable S
  LEFT JOIN keyTable K
WHERE
  S.keyid = K.keyid
  AND K.keyid = ?;
