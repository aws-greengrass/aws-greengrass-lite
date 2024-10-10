WITH RECURSIVE
  path_cte (current_key_id, depth) AS (
    SELECT
      keyid,
      1
    FROM
      keyTable
    WHERE
      keyid NOT IN (
        SELECT
          keyid
        FROM
          relationTable
      )
      AND keyvalue = ?
    UNION ALL
    SELECT
      kt.keyid,
      pc.depth + 1
    FROM
      path_cte pc
      JOIN relationTable rt ON pc.current_key_id = rt.parentid
      JOIN keyTable kt ON rt.keyid = kt.keyid
    WHERE
      kt.keyvalue = (
        CASE pc.depth
          WHEN 1 THEN ?
          WHEN 2 THEN ?
          WHEN 3 THEN ?
          WHEN 4 THEN ?
          WHEN 5 THEN ?
          WHEN 6 THEN ?
          WHEN 7 THEN ?
          WHEN 8 THEN ?
          WHEN 9 THEN ?
          WHEN 10 THEN ?
          WHEN 11 THEN ?
          WHEN 12 THEN ?
          WHEN 13 THEN ?
          WHEN 14 THEN ?
        END
      )
      AND pc.depth < ?
  )
SELECT
  current_key_id AS key_id
FROM
  path_cte
