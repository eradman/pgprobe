/*
 * Eric Radman, 2019
 */

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <libpq-fe.h>

#include "pgprobe.h"

char hostname[50];

/* forwards */
int timedelta(struct timespec *tv_start, struct timespec *tv_end);
int log_response(PGconn *probe_conn, Node *node);

/* pgprobe-query */

int
main(int argc, char *argv[]) {
	char *p;
	Node node;

	PGconn *conn;
	PGresult *res;
	const char *paramValues[1];
	int paramLengths[1];
	int paramFormats[1];

	gethostname(hostname, sizeof(hostname));
	p = strchr(hostname, '.');
	*p = '\0';
	bzero(&node, sizeof(Node));

	if (argc != 3)
		errx(1, "usage: pgprobe-query logdb_url node_name"); 

	conn = PQconnectdb(argv[1]);
	if(PQstatus(conn) == CONNECTION_BAD)
	        errx(1, "Connection failed: %s", PQerrorMessage(conn));

	paramValues[0] = argv[2];
	paramLengths[0] = strlen(argv[2]);
	paramFormats[0] = 0;

	res = PQexecParams(conn,
		"SELECT database_id, name, url, remain_idle, test_query "
		"FROM probe_rules "
		"WHERE name=$1::varchar", 1,
		NULL, paramValues, paramLengths, paramFormats, 0
	);
	if(PQresultStatus(res) != PGRES_TUPLES_OK)
		errx(1, "SELECT failed %s", PQerrorMessage(conn));

	if (PQntuples(res) == 0)
		errx(1, "pgprobe: unable to find %s", argv[2]);

	node.id = atoi(PQgetvalue(res, 0, 0));
	strlcpy(node.name, PQgetvalue(res, 0, 1), sizeof(node.name));
	strlcpy(node.url, PQgetvalue(res, 0, 2), sizeof(node.url));
	node.remain_idle = atoi(PQgetvalue(res, 0, 3));
	strlcpy(node.test_query, PQgetvalue(res, 0, 4), sizeof(node.test_query));
	PQclear(res);

	if (!log_response(conn, &node)) {
		/* avoid thrashing */
		sleep(2);
	}
	PQfinish(conn);

	return 0;

}

int timedelta(struct timespec *tv_start, struct timespec *tv_end) {
	int start, end;

	/* time in milliseconds */
	start = (tv_start->tv_sec * 1000) + (tv_start->tv_nsec / 1000000);
	end = (tv_end->tv_sec * 1000) + (tv_end->tv_nsec / 1000000);

	return end - start;
}

int log_response(PGconn *probe_conn, Node *node) {
	struct timespec tv_start, tv_end;
	int ok;
	char in_recovery[2];
	int query_time_ms, conn_time_ms;
	char error_message[256];
	char *p;

	const char *steps[] = { "CONNECT", "QUERY_IN_RECOVERY", "QUERY_1" };
	int step;

	PGconn *conn;
	PGresult *res;
	uint32_t uint32_params[3];
	int total_sleep;

	const char *paramValues[7];
	int paramLengths[7];
	int paramFormats[7];

	/* initial values */
	total_sleep = 0;
	ok = 1;
	strlcpy(in_recovery, "f", 2);
	error_message[0] = '\0';

	for (step=0; step<3; step++) {
		switch(step) {
		case 0:
			/* connect */
			clock_gettime(CLOCK_MONOTONIC, &tv_start);
			conn = PQconnectdb(node->url);
			if(PQstatus(conn) == CONNECTION_BAD) {
				ok = 0;
			}
			clock_gettime(CLOCK_MONOTONIC, &tv_end);
			conn_time_ms = timedelta(&tv_start, &tv_end);

			/* query, idle, query */
			clock_gettime(CLOCK_MONOTONIC, &tv_start);
			if (!ok) goto finish;
			break;

		case 1:
			res = PQexec(conn, "SELECT pg_is_in_recovery()");
			if(PQresultStatus(res) != PGRES_TUPLES_OK) {
				ok = 0;
				goto finish;
			}
			strlcpy(in_recovery, PQgetvalue(res, 0, 0), 2);
			PQclear(res);

			total_sleep = node->remain_idle - sleep(node->remain_idle);
			break;

		case 2:
			res = PQexec(conn, node->test_query);
			if (PQresultStatus(res) != PGRES_TUPLES_OK) {
				ok = 0;
				goto finish;
			}
			break;
		}
	}
	PQclear(res);

finish:
	/* error_string */
	strlcpy(error_message, PQerrorMessage(conn), sizeof(error_message));
	p = strchr(error_message, '\n');
	if (p) *p = '\0';  /* drop subsequent lines of explanation */
	PQfinish(conn);

	/* query timing */
	clock_gettime(CLOCK_MONOTONIC, &tv_end);
	query_time_ms = timedelta(&tv_start, &tv_end) - (total_sleep * 1000);

	uint32_params[0] = htonl(node->id);
	paramValues[0] = (char *)&uint32_params[0];
	paramLengths[0] = sizeof(uint32_params[0]);
	paramFormats[0] = 1;

	paramValues[1] = in_recovery;
	paramLengths[1] = strlen(in_recovery);
	paramFormats[1] = 0;

	uint32_params[1] = htonl(conn_time_ms);
	paramValues[2] = (char *)&uint32_params[1];
	paramLengths[2] = sizeof(uint32_params[1]);
	paramFormats[2] = 1;

	uint32_params[2] = htonl(query_time_ms);
	paramValues[3] = (char *)&uint32_params[2];
	paramLengths[3] = sizeof(uint32_params[2]);
	paramFormats[3] = 1;

	paramValues[4] = strlen(error_message) ? error_message : NULL;
	paramLengths[4] = strlen(error_message);
	paramFormats[4] = 0;

	paramValues[5] = hostname;
	paramLengths[5] = strlen(hostname);
	paramFormats[5] = 0;

	paramValues[6] = step < 3 ? steps[step] : NULL;
	paramLengths[6] = step < 3 ? strlen(steps[step]) : 0;
	paramFormats[6] = 0;

	res = PQexecParams(probe_conn,
		"INSERT INTO response_log                              "
		"  (database_id, in_recovery, connect_time, query_time,"
		"   error_message, hostname, check_step)               "
		"VALUES                                                "
		"  ($1::int, $2::bool, $3::int, $4::int, $5::varchar,  "
		"   $6::varchar, $7::step)                             ", 7,
		NULL, paramValues, paramLengths, paramFormats, 0);
	/* self-terminate if we were unable to record the result */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		fprintf(stderr, "INSERT failed: %s", PQerrorMessage(probe_conn));
		exit(3);
	}

	return ok;
}
