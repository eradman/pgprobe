#include <signal.h>

#if defined(__GLIBC__)
#define strlcpy(dst, src, dstsize) memcpy(dst, src, dstsize)
#endif

typedef struct {
	int pid;
	int id;
	char name[32];
	char url[1024];
	int remain_idle;
	char test_query[2048];
} Node;

#define RELOAD_SIG SIGHUP
