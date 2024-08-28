-- probe_rules
--
CREATE TYPE step AS ENUM ('CONNECT', 'QUERY_IN_RECOVERY', 'QUERY_1');

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

GRANT SELECT ON probe_rules TO report;

-- response_log
--
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

GRANT SELECT, INSERT, DELETE ON response_log TO report;
