CREATE OR REPLACE FUNCTION expire_rows()
RETURNS bool AS $$
  DELETE FROM response_log
  USING probe_rules
  WHERE probe_rules.database_id=response_log.database_id
  AND event_time < now() - probe_rules.expire
  RETURNING true;
$$
LANGUAGE sql;

SELECT name, active, database_id, now()-min(event_time) AS first_record, expire
FROM probe_rules
LEFT JOIN response_log using (database_id)
GROUP BY expire, database_id;
