#!/bin/sh
# Eric Radman, 2019

trap 'printf "$0: exit code $? on line $LINENO\n"; kill $!; exit 1' ERR
cd "$(dirname $0)/.."
function log {
	msg="$(date '+%H:%M:%S') $*"
	printf "\e[7m${msg}\e[27m\n"
}

log "starting test database"
url=$(pg_tmp)
psql="psql -P footer=off -P linestyle=unicode --no-psqlrc -q -v ON_ERROR_STOP=1 $url"
$psql -f schema/roles.sql
$psql -At <<-SQL
	SELECT setting || '/postgres.log' AS logfile
	FROM pg_settings
	WHERE name = 'data_directory'
SQL

log "loading schema"
for sql in $(ls -d schema/0*); do
	$psql -f $sql -o /dev/null
done
$psql <<-SQL
	INSERT INTO probe_rules (name, url, remain_idle, active)
	VALUES ('pg_tmp', '${url}&application_name=probe_test', 2, 't');
SQL

log "starting pgprobe"
src/pgprobe $url &
sleep 0.5

log "show process tree"
pstree $!

log "terminating a single connection"
sleep 0.5
$psql <<-SQL
	SELECT pid,state,application_name,pg_terminate_backend(pid) FROM pg_stat_activity
	WHERE usename is not null
	AND application_name = 'probe_test'
	LIMIT 1;
SQL

log "report events"
sleep 4
$psql <<SQL
	SELECT event_time,in_recovery,connect_time,query_time,error_message,check_step
	FROM response_log
	ORDER BY event_time;
SQL

log "Deactive rules to trigger reload/abort"
sleep 4
$psql -c "UPDATE probe_rules SET active='f'"

wait
log "OK"
