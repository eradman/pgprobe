/*
 * Eric Radman, 2019
 */

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "libpq-fe.h"

#include "pgprobe.h"

/* pgprobe-reload */

int
main(int argc, char **argv)
{
	PGconn *conn;
	PGresult *res;
	PGnotify *notify;
	int nnotifies;
	int pid;

	if (argc != 3)
		errx(1, "usage: pgprobe-reload logdb_url pid"); 
	pid = atoi(argv[2]);
	conn = PQconnectdb(argv[1]);

	if (PQstatus(conn) != CONNECTION_OK)
		errx(1, "Connection to database failed: %s", PQerrorMessage(conn));

	res = PQexec(conn, "LISTEN rule_channel");
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
		errx(1, "LISTEN command failed: %s", PQerrorMessage(conn));

	PQclear(res);

	/* Quit after four notifies are received; parent should re-launch */
	nnotifies = 0;
	while (nnotifies < 4)
	{
		int sock;
		fd_set input_mask;

		sock = PQsocket(conn);

		if (sock < 0) {
			warnx("PQsocket failed");
			break;
		}

		FD_ZERO(&input_mask);
		FD_SET(sock, &input_mask);

		if (select(sock + 1, &input_mask, NULL, NULL, NULL) < 0)
			errx(1, "select() failed: %s", strerror(errno));

		if (PQconsumeInput(conn) == 0)
			warnx("%s", PQerrorMessage(conn));
		while ((notify = PQnotifies(conn)) != NULL)
		{
			kill(pid, RELOAD_SIG);
			PQfreemem(notify);
			nnotifies++;
			if (PQconsumeInput(conn) == 0)
				warnx("%s", PQerrorMessage(conn));
		}
	}

	PQfinish(conn);

	return 0;
}
