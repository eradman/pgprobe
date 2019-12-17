/*
 * Eric Radman, 2019
 */

#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libpq-fe.h>

#include "pgprobe.h"

int reload;

/* forwards */
void handle_reload(int sig);
int run_workers(const char *self, char *logdb_url);


/* pgprobe */

void
handle_reload(int sig) {
	reload=1;
}

int
main(int argc, char *argv[]) {
	struct sigaction act;

	if (argc != 2)
		errx(1, "usage: pgprobe logdb_url"); 

	act.sa_flags = 0;
	act.sa_handler = handle_reload;
	if (sigemptyset(&act.sa_mask) & (sigaction(RELOAD_SIG, &act, NULL) != 0))
		err(1, "Failed to set handler for signal %d", RELOAD_SIG);

	return run_workers(argv[0], argv[1]);
}

/*
 * return codes:
 *  1 - A general error
 *  2 - Could not connect to logging database
 *  3 - Failed to record a log entry
 */
int
run_workers(const char* self, char *logdb_url) {
	int n;
	int status;
	int node_count;
	char buf[20];
	char prog[1024];

	int reload_pid = 0;

	Node *nodes;

	PGconn *conn;
	PGresult *res;

reload:
	conn = PQconnectdb(logdb_url);
	if(PQstatus(conn) == CONNECTION_BAD)
		errx(1, "Connection failed: %s", PQerrorMessage(conn));

	/* read in list of hosts */
	res = PQexec(conn,
		"SELECT database_id, name, url, remain_idle "
		"FROM probe_rules "
		"WHERE active='t'"
	);
	if(PQresultStatus(res) != PGRES_TUPLES_OK)
		errx(1, "SELECT failed %s", PQerrorMessage(conn));

	node_count = PQntuples(res);
	if (node_count == 0)
		errx(1, "pgprobe: no active nodes defined");

	/* Allocate one more for our probeing process */
	nodes = malloc((node_count+1) * sizeof(Node));
	bzero(nodes, (node_count+1) * sizeof(Node));

	for (n = 0; n < node_count; n++) {
		nodes[n].pid = 0;
		nodes[n].id = atoi(PQgetvalue(res, n, 0));
		strlcpy(nodes[n].name, PQgetvalue(res, n, 1), sizeof(nodes[n].name));
		strlcpy(nodes[n].url, PQgetvalue(res, n, 2), sizeof(nodes[n].url));
		nodes[n].remain_idle = atoi(PQgetvalue(res, n, 3));
	}
	PQclear(res);

restart_children:
	/* expire old records */
	res = PQexec(conn, "SELECT expire_rows()");
	if(PQresultStatus(res) != PGRES_TUPLES_OK)
		errx(1, "expire_rows() failed %s", PQerrorMessage(conn));
	PQclear(res);

	/* pgprobe-query */
	for (n = 0; n<node_count; n++) {
		if (nodes[n].pid == 0) {
			nodes[n].pid = fork();
			if (nodes[n].pid == -1)
				err(1, "fork");
			if (nodes[n].pid == 0) {
				snprintf(prog, sizeof(prog), "%s%s", self, "-query");
				printf("%d probeing %s\n", getpid(), nodes[n].name);
				execl(prog, "pgprobe-query", logdb_url,
					nodes[n].name, NULL);
				err(1, "execl failed");
			}
		}
		if (waitpid(nodes[n].pid, &status, WNOHANG) == -1) {
			printf("%d ended with status %d\n", nodes[n].pid,
				WEXITSTATUS(status));
			nodes[n].pid = 0;
		}

	}
	/* pgprobe-reload */
	if (reload_pid == 0) {
		reload_pid = fork();
		if (reload_pid == -1)
			err(1, "fork");
		if (reload_pid == 0) {
			snprintf(buf, sizeof(buf), "%d", getppid());
			snprintf(prog, sizeof(prog), "%s%s", self, "-reload");
			printf("auto-reload will signal %s\n", buf);
			execl(prog, "pgprobe-reload", logdb_url,
				buf, NULL);
			err(1, "execl failed");
		}
	}
	if (waitpid(reload_pid, &status, WNOHANG) == -1) {
		printf("auto-reload ended with status %d\n", WEXITSTATUS(status));
		reload_pid = 0;
	}

	sleep(2);
	if (reload == 1) {
		reload = 0;
		printf("reload requested; signaling workers\n");
		for (n = 0; n < node_count; n++) {
			if (nodes[n].pid > 0) {
				kill(nodes[n].pid, SIGTERM);
				waitpid(nodes[n].pid, &status, WNOHANG);
			}
			free(nodes);
			goto reload;
		}
	}
	goto restart_children;
	return 1;
}
