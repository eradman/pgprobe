-- types

CREATE TYPE step AS ENUM ('CONNECT', 'QUERY_IN_RECOVERY', 'QUERY_1');

-- tables

CREATE TABLE probe_rules (
    database_id serial PRIMARY KEY,
    category varchar(31) NOT NULL DEFAULT 'cluster',
    name varchar(31) NOT NULL UNIQUE,
    url varchar(1023) NOT NULL,
    remain_idle int default 10 NOT NULL,
    active bool NOT NULL DEFAULT 'f',
    expire interval NOT NULL DEFAULT '1 week',
    test_query varchar(2047) NOT NULL DEFAULT 'SELECT 1'
);

CREATE TABLE response_log (
    hostname varchar(31) NOT NULL,
    database_id int REFERENCES probe_rules(database_id) NOT NULL,
    event_time timestamp with time zone DEFAULT now(),
    in_recovery bool,
    connect_time int NOT NULL,
    query_time int NULL,
    error_message varchar,
    check_step step
);

CREATE INDEX ON response_log(event_time);

/* functions */

CREATE OR REPLACE FUNCTION rule_notify()
  RETURNS trigger AS
$$
  BEGIN
    PERFORM pg_notify('rule_channel', NEW.name);
    RETURN NULL;
  END;
$$ LANGUAGE plpgsql;;

/* triggers */

CREATE TRIGGER rule_notify
AFTER INSERT OR UPDATE OR DELETE ON probe_rules
FOR EACH ROW EXECUTE PROCEDURE rule_notify();
CREATE OR REPLACE FUNCTION expire_rows()
RETURNS bool AS $$
  DELETE FROM response_log
  USING probe_rules
  WHERE probe_rules.database_id=response_log.database_id
  AND event_time < now() - probe_rules.expire
  RETURNING true;
$$
LANGUAGE sql;
