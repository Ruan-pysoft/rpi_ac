#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#define GPIO_IMPLEMENTATION
#include "gpio.h"

#define RRP_APP_IMPL
#include "../htmlserver/protocol/rrp_app.h"

extern int sigint_handler_gpiofd;
void sigint_handler(int);

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

int main(void) {
	/** SOCKET **/

	int sockfd;

	{
		snprintf(rundir, 1024, "/run/user/%d/", getuid());
		rundir_len = strlen(rundir);

		sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
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
	}

	/** LED **/

	int ledfd;

	{
		int chipfd = open("/dev/gpiochip0", O_RDWR);
		if (chipfd == -1) {
			perror("reading GPIO chip 0");
			return 1;
		}

		ledfd = get_gpio_pin_output(chipfd, 26);
		close(chipfd);
		if (ledfd == -1) {
			perror("accessing LED GPIO pin");
			return 1;
		}

		sigint_handler_gpiofd = ledfd;
		if (signal(SIGINT, sigint_handler) == SIG_ERR) {
			close(ledfd);

			perror("setting quit handler");
			return 1;
		}
	}

	/** EVENTLOOP **/

	for (;;) {
		struct sockaddr_un serveraddr;
		socklen_t serveraddrlen = sizeof(serveraddr);
		int serverfd = accept(sockfd, (void*)&serveraddr, &serveraddrlen);
		if (serverfd == -1) {
			close(sockfd);
			perror("failed accepting connection");
			sigint_handler(0);
		}

		struct Request req;
		if (request_recv(serverfd, &req) != 0) {
			request_delete(&req);
			struct Response resp = response_from_protocol_error(protocol_error);
			response_send(serverfd, resp);

			close(serverfd);

			continue;
		}

		switch (req.type) {
		case RQT_GET: {
			struct Response resp = {
				.status = "200 OK",
				.mimetype = "text/html",
			};

			printf("Got GET request on path `%s`.\n", req.path);

			if (strcmp(req.path, "on") == 0) {
				printf(" - `%s` == `on`\n", req.path);
				set_gpio_pin(ledfd, true);
				resp.body = "<h1>Put LED on!</h1>";
			} else if (strcmp(req.path, "off") == 0) {
				printf(" - `%s` == `off`\n", req.path);
				set_gpio_pin(ledfd, false);
				resp.body = "<h1>Put LED off!</h1>";
			} else {
				printf(" - `%s` == ?\n", req.path);
				resp.body = "<h1>You have reached the LED interface</h1>\n"
					"<ul><li><a href='/app/on'>Put LED on</a></li>\n"
					"<li><a href='/app/off'>Put LED off</a></li></ul>\n"
				;
			}
			resp.content_length = strlen(resp.body);

			response_send(serverfd, resp);
		} break;
		case RQT_POST: {
			assert(false && "not implemented");
		} break;
		case RQT_PING: {
			struct Response resp = response_from_ping(&req);
			response_send(serverfd, resp);
		} break;
		}

		request_delete(&req);
		close(serverfd);
	}

	set_gpio_pin(ledfd, false);
	close(ledfd);
}

void fsleep(float secs) {
	usleep(secs * 1000 * 1000);
}

int sigint_handler_gpiofd;
void sigint_handler(int) {
	puts("Quitting!");
	puts("putting LED off...");

	bool on = false;
	for (int i = 0; i < 25; ++i) {
		set_gpio_pin(sigint_handler_gpiofd, on);
		on = !on;
		fsleep(1 / 20.f);
	}

	set_gpio_pin(sigint_handler_gpiofd, false);
	_exit(1);
}
