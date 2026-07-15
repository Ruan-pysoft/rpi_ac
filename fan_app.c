// SEE: https://randu.org/tutorials/threads/

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#define GPIO_IMPLEMENTATION
#include "rpi_interface/gpio.h"
#define THERMSENSE_IMPLEMENTATION
#include "rpi_interface/thermsense.h"

#define RRP_APP_IMPL
#include "htmlserver/protocol/rrp_app.h"

const char *kantoor_id = "28-0516a35791ff";
const char *dak_id = "28-0516a43debff";

enum fan_state {
	FS_AUTOMATIC = 0,
	FS_FORCE_ON,
	FS_FORCE_OFF,
};

const char *fan_state_map[] = {
	[FS_AUTOMATIC] = "automatic",
	[FS_FORCE_ON] = "forced on",
	[FS_FORCE_OFF] = "forced off",
};

static struct mutable_state {
	pthread_mutex_t lock;

	enum fan_state fan_state;
	float min_temp_diff;
	int check_every;

	bool fan_enabled;

	int exit_code;
} mutable_state = {
	.lock = PTHREAD_MUTEX_INITIALIZER,

	.fan_state = FS_AUTOMATIC,
	.min_temp_diff = 5,
	.check_every = 5 * 60,

	.fan_enabled = false,

	.exit_code = 1,
};

#define create_getset(type, field) \
	type get_##field(void) {\
		pthread_mutex_lock(&mutable_state.lock); \
		type field = mutable_state.field; \
		pthread_mutex_unlock(&mutable_state.lock); \
		return field; \
	} \
	void set_##field(type field) { \
		pthread_mutex_lock(&mutable_state.lock); \
		mutable_state.field = field; \
		pthread_mutex_unlock(&mutable_state.lock); \
	}

create_getset(enum fan_state, fan_state)
create_getset(float, min_temp_diff)
create_getset(int, check_every)
create_getset(int, exit_code)

bool is_fan_enabled(void) {
	pthread_mutex_lock(&mutable_state.lock);
	bool fan_enabled = mutable_state.fan_enabled;
	pthread_mutex_unlock(&mutable_state.lock);
	return fan_enabled;
}
int fanfd;
void set_fan_enabled(bool enabled) {
	set_gpio_pin(fanfd, enabled);
	pthread_mutex_lock(&mutable_state.lock);
	mutable_state.fan_enabled = enabled;
	pthread_mutex_unlock(&mutable_state.lock);
}

bool should_enable_fan(void) {
	pthread_mutex_lock(&mutable_state.lock);

	puts("Checking if FAN should be on...");
	printf("Fan state is %s!\n", fan_state_map[mutable_state.fan_state]);
	bool should_enable;
	switch (mutable_state.fan_state) {
		case FS_AUTOMATIC: {
			puts("  Reading sensor temperatures...");
			const float kantoor_temp = get_temperature(kantoor_id);
			printf("  Sensor [kantoor] has temp %f!\n", kantoor_temp);
			const float dak_temp = get_temperature(dak_id);
			printf("  Sensor [dak] has temp %f!\n", dak_temp);

			const float min_temp = kantoor_temp + mutable_state.min_temp_diff;
			should_enable = dak_temp >= min_temp;
		} break;
		case FS_FORCE_ON: {
			puts("  Forced on!");
			should_enable = true;
		} break;
		case FS_FORCE_OFF: {
			puts("  Forced off!");
			should_enable = false;
		} break;
	}
	printf("FAN should be %s.\n", should_enable ? "on" : "off");

	pthread_mutex_unlock(&mutable_state.lock);
	return should_enable;
}

struct mutable_state copy_mutable_state(void) {
	pthread_mutex_lock(&mutable_state.lock);
	struct mutable_state res = mutable_state;
	res.lock = (pthread_mutex_t) {0};
	pthread_mutex_unlock(&mutable_state.lock);
	return res;
}

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

pthread_t fan_controller_thread;
void *fan_controller_fn(void *);
pthread_t socket_server_thread;
void *socket_server_fn(void *sockfdptr);

int main(void) {
	snprintf(rundir, 1024, "/run/user/%d/", getuid());
	rundir_len = strlen(rundir);

	/** SOCKET **/

	int sockfd;

	{
		sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sockfd == -1) {
			perror("couldn't create socket");
			return 1;
		}
		printf("sockfd: %d\n", sockfd);

		const char *sockfname = get_runfile("rrp_test_app.sock");

		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, sockfname);
		unlink(sockfname);
		free((char*)sockfname);
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

	/** SENSORS **/

	sensor_name_t sensors[16];
	size_t sensors_len = get_sensors(sensors, sizeof(sensors)/sizeof(*sensors));

	puts("Found sensors:");
	for (size_t i = 0; i < sensors_len; ++i) {
		printf(" - %s", sensors[i]);
		if (strcmp(sensors[i], kantoor_id)) printf(" [kantoor]");
		else if (strcmp(sensors[i], dak_id)) printf(" [dak]");
		putchar('\n');
	}

	puts("");

	puts("Temperatures:");
	for (size_t i = 0; i < sensors_len; ++i) {
		printf(" - %f\n", get_temperature(sensors[i]));
	}

	/** FAN **/

	{
		int chipfd = open("/dev/gpiochip0", O_RDWR);
		if (chipfd == -1) {
			close(sockfd);

			perror("reading GPIO chip 0");
			return 1;
		}

		fanfd = get_gpio_pin_output_inverted(chipfd, 26);
		close(chipfd);
		if (fanfd == -1) {
			close(sockfd);

			perror("accessing LED GPIO pin");
			return 1;
		}

		sigint_handler_gpiofd = fanfd;
		if (signal(SIGINT, sigint_handler) == SIG_ERR) {
			close(sockfd);
			close(fanfd);

			perror("setting quit handler");
			return 1;
		}
	}

	/** THREADS **/

	puts("Creating fan controller thread!");
	errno = pthread_create(&fan_controller_thread, NULL, fan_controller_fn, NULL);
	if (errno) {
		perror("creating fan-controlling thread");
		mutable_state.exit_code = 1;
		goto cleanup;
	}
	puts("Created fan controller thread!");
	//sleep(5);

	puts("Creating socket server thread!");
	errno = pthread_create(&socket_server_thread, NULL, socket_server_fn, &sockfd);
	if (errno) {
		pthread_cancel(fan_controller_thread);

		perror("creating socket server thread");
		mutable_state.exit_code = 1;
		goto cleanup;
	}
	puts("Created socket server thread!");
	//sleep(5);

	puts("Waiting for fan controller to exit...");
	pthread_join(fan_controller_thread, NULL);
	puts("Waiting for socket server to exit...");
	pthread_join(socket_server_thread, NULL);
	puts("All threads exited!");

	/** CLEANUP **/
cleanup:
	close(sockfd);
	set_gpio_pin(fanfd, false);
	close(fanfd);

	return mutable_state.exit_code;
}

int sigint_handler_gpiofd;
void sigint_handler(int) {
	puts("Quitting!");
	puts("putting FAN off...");

	set_gpio_pin(sigint_handler_gpiofd, false);
	_exit(1);
}

void *fan_controller_fn(void *) {
	for (;;) {
		set_fan_enabled(should_enable_fan());

		sleep(get_check_every());
	}

	pthread_exit(NULL);
}

char body_buf[1024 * 1024];
void create_response_body(struct Response *resp, const char *path) {
	if (strcmp(path, "on") == 0) {
		printf(" - `%s` == `on`\n", path);
		set_fan_state(FS_FORCE_ON);
		set_fan_enabled(true);
		resp->body =
			"<h1>Forced fan on!</h1>\n"
			"<p><a href='/fan'>Return to fan interface</a></p>\n"
		;
	} else if (strcmp(path, "off") == 0) {
		printf(" - `%s` == `off`\n", path);
		set_fan_state(FS_FORCE_OFF);
		set_fan_enabled(false);
		resp->body =
			"<h1>Forced fan off!</h1>\n"
			"<p><a href='/fan'>Return to fan interface</a></p>\n"
		;
	} else if (strcmp(path, "auto") == 0) {
		printf(" - `%s` == `auto`\n", path);
		set_fan_state(FS_AUTOMATIC);
		set_fan_enabled(should_enable_fan());
		resp->body =
			"<h1>Fan set to automatic!</h1>\n"
			"<p><a href='/fan'>Return to fan interface</a></p>\n"
		;
	} else if (strcmp(path, "state") == 0) {
		printf(" - `%s` == `state`\n", path);
		puts("   Copying state...");
		struct mutable_state state = copy_mutable_state();
		puts("  Reading temperatures...");
		const float kantoor_temp = get_temperature(kantoor_id);
		const float dak_temp = get_temperature(dak_id);
		puts("  Preparing body...");
		snprintf(body_buf, sizeof(body_buf),
			"<h1>Fan system state</h1>\n"
			"<ul>\n"
			"<li>Fan state: %s (currently %s)</li>\n"
			"<li>Kantoor must be %f °C colder than dak to automatically enable the fan.</li>\n"
			"<li>The system checks if the fan's state must be updated every %d seconds.</li>\n"
			"</ul>\n"
			"<h2>Temperatures:</h2>\n"
			"<ul>\n"
			"<li>Kantoor: %f</li>\n"
			"<li>Dak: %f</li>\n"
			"</ul>\n"
			"<p><a href='/fan'>Return to fan interface</a></p>\n"
			, fan_state_map[state.fan_state], state.fan_enabled ? "on" : "off"
			, state.min_temp_diff
			, state.check_every
			, kantoor_temp
			, dak_temp
		);
		resp->body = body_buf;
		puts("  Done!");
	} else {
		printf(" - `%s` == ?\n", path);
		resp->body =
			"<h1>You have reached the fan interface</h1>\n"
			"<ul><li><a href='/fan/on'>Force fan on</a></li>\n"
			"<li><a href='/fan/off'>Force fan off</a></li>\n"
			"<li><a href='/fan/off'>Set fan to automatic</a></li>\n"
			"<li><a href='/fan/state'>See information on the fan's state</a></li></ul>\n"
		;
	}
	resp->content_length = strlen(resp->body);
}

void *socket_server_fn(void *sockfdptr) {
	int sockfd = *(int *)sockfdptr;

	for (;;) {
		int serverfd = accept(sockfd, NULL, NULL);
		if (serverfd == -1) {
			perror("failed accepting connection");
			pthread_cancel(fan_controller_thread);
			set_exit_code(1);
			break;
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

			create_response_body(&resp, req.path);
			response_send(serverfd, resp);
		} break;
		case RQT_POST: {
			// TODO: shouldn't assert here...
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

	pthread_exit(NULL);
}
