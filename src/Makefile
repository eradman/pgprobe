INCS = -I `pg_config --includedir`
LIBS = -L `pg_config --libdir` -lpq
CPPFLAGS = -std=c99 -pedantic -Wall -Wpointer-arith -Wbad-function-cast -g
CFLAGS = -D_GNU_SOURCE ${INCS}
LDFLAGS = ${LIBS}
PROGS = pgprobe pgprobe-query pgprobe-reload

all: ${PROGS}

clean:
	rm -f ${PROGS}

.PHONY: all clean
