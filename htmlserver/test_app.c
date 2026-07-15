#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

char rundir[1024];
int rundir_len;

const char *get_runfile(const char *filename) {
	int fnamelen = strlen(filename);

	char *res = malloc(rundir_len + fnamelen + 1);
	assert(res != NULL);

	strcpy(res, rundir);
	strcpy(res+rundir_len, filename);

	return res;
}

int main() {
	puts("Hello, world!");

	snprintf(rundir, 1024, "/run/user/%d/", getuid());
	rundir_len = strlen(rundir);

	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("couldn't create socket");
		return 1;
	}
	printf("%d\n", sockfd);

	const char *sockfname = get_runfile("rrp_test_app.sock");

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockfname);
	unlink(sockfname);
	int bindres = bind(sockfd, (void*)&addr, sizeof(addr));
	if (bindres == -1) {
		close(sockfd);
		perror("failed to bind socket");
		return 1;
	}

	if (listen(sockfd, 16) == -1) {
		close(sockfd);
		perror("failed to listen on socket");
		return 1;
	}

	struct sockaddr_un serveraddr;
	socklen_t serveraddrlen = sizeof(serveraddr);
	int serverfd = accept(sockfd, (void*)&serveraddr, &serveraddrlen);
	if (serverfd == -1) {
		close(sockfd);
		perror("failed accepting connection");
		return 1;
	}

	char buf[1024];

	for (;;) {
		ssize_t recvd = recv(serverfd, buf, 1024, 0);
		if (recvd == -1) {
			perror("got error reading from socket");
			return 1;
		}
		if (recvd == 0) break;

		printf("Got: %.*s", (int)recvd, buf);
		assert(send(serverfd, "Got: ", 5, 0) == 5);
		assert(send(serverfd, buf, recvd, 0) == recvd);
	}

	close(serverfd);
	close(sockfd);
}
