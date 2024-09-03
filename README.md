PgProbe
-------

Verify PostgreSQL connectivity

1. New connections succeed, but are slow to run a simple query
2. An existing connection is dropped
3. New connections are denied or time out
4. Database is paused
5. Database is read-only

Architecture
------------

Each health check is run as an independent worker, and a reload process signals
the parent if the `node` table is updated.

* The main process, `pgprobe` collects the set of hosts to monitor from `node`
  table.
* For each database and instance of `pgprobe-query` updates the `response_log`
  table.
* If the `node` table is updated, `pgprobe-reload` signals `pgprobe` to restart
  it's workers.

Initial Configuration
---------------------

Initialize database

    psql -f schema/roles.sql
    psql -c 'CREATE DATABASE pgprobe OWNER pgprobe;'
    psql -c 'ALTER USER pgprobe SUPERUSER;'
    for f in schema/??-*.sql; do
        psql -q -U pgprobe -f $f
    done
    psql -c 'ALTER USER pgprobe NOSUPERUSER;'

Optionally add a partition for each monitored host

    CREATE TABLE response_log_test PARTITION OF response_log FOR VALUES IN ('svc1');
    CREATE TABLE response_log_test PARTITION OF response_log FOR VALUES IN ('svc2');

Give pgprobe access to it's own database by writing a password file as the
user `postgres`:

    ssh db3
    echo '*:*:pgprobe:report:XXXXXX' >> .pgprobe
    chmod 600 .pgprobe

Monitoring a New Host
---------------------

    INSERT INTO probe_rules (name,url,active,category) VALUES
      ('sidecomment', 'postgresql://webui@svc1.sidecomment.io/sidecomment', 't', 'www');

Limitations
-----------

pgprobe does not handle the case where a connection remains valid, but the
current transaction was aborted.
