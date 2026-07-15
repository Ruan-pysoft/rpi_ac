// SEE: https://randu.org/tutorials/threads/

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define GPIO_IMPLEMENTATION
#include "rpi_interface/gpio.h"
#define THERMSENSE_IMPLEMENTATION
#include "rpi_interface/thermsense.h"

enum fan_state {
	FS_AUTOMATIC = 0,
	FS_FORCE_ON,
	FS_FORCE_OFF,
};

static struct {
	pthread_mutex_t lock;

	enum fan_state fan_state;
	float min_temp_diff;
	int check_every;

	int exit_code;
} mutable_state = {
	.lock = PTHREAD_MUTEX_INITIALIZER,

	.fan_state = FS_AUTOMATIC,
	.min_temp_diff = 5,
	.check_every = 5 * 60,

	.exit_code = 1,
};

const char *kantoor_id = "28-0516a35791ff";
const char *dak_id = "28-0516a43debff";

extern int sigint_handler_gpiofd;
void sigint_handler(int);

pthread_t fan_controller_thread;
void *fan_controller_fn(void *fanfdptr);
pthread_t socket_server_thread;
void *socket_server_fn(void *);

int main(void) {
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

	int fanfd;

	{
		int chipfd = open("/dev/gpiochip0", O_RDWR);
		if (chipfd == -1) {
			perror("reading GPIO chip 0");
			return 1;
		}

		fanfd = get_gpio_pin_output_inverted(chipfd, 26);
		close(chipfd);
		if (fanfd == -1) {
			perror("accessing LED GPIO pin");
			return 1;
		}

		sigint_handler_gpiofd = fanfd;
		if (signal(SIGINT, sigint_handler) == SIG_ERR) {
			close(fanfd);

			perror("setting quit handler");
			return 1;
		}
	}

	/** THREADS **/

	errno = pthread_create(&fan_controller_thread, NULL, fan_controller_fn, &fanfd);
	if (errno) {
		perror("creating fan-controlling thread");
		mutable_state.exit_code = 1;
		goto cleanup;
	}
	errno = pthread_create(&socket_server_thread, NULL, socket_server_fn, NULL);
	if (errno) {
		pthread_cancel(fan_controller_thread);

		perror("creating socket server thread");
		mutable_state.exit_code = 1;
		goto cleanup;
	}

	pthread_join(fan_controller_thread, NULL);
	pthread_join(socket_server_thread, NULL);

	/** CLEANUP **/

cleanup:
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

void *fan_controller_fn(void *fanfdptr) {
	int fanfd = *(int *)fanfdptr;

	for (;;) {
		puts("Checking if FAN should be on...");

		const float kantoor_temp = get_temperature(kantoor_id);
		printf("  Sensor [kantoor] has temp %f!\n", kantoor_temp);
		const float dak_temp = get_temperature(dak_id);
		printf("  Sensor [dak] has temp %f!\n", dak_temp);

		pthread_mutex_lock(&mutable_state.lock);

		bool enable;
		switch (mutable_state.fan_state) {
		case FS_AUTOMATIC: {
			enable = dak_temp >= kantoor_temp + mutable_state.min_temp_diff;
		} break;
		case FS_FORCE_ON: {
			enable = true;
		} break;
		case FS_FORCE_OFF: {
			enable = false;
		} break;
		}
		printf("FAN should be %s.\n", enable ? "on" : "off");

		int sleep_length = mutable_state.check_every;

		pthread_mutex_unlock(&mutable_state.lock);

		set_gpio_pin(fanfd, enable);

		sleep(sleep_length);
	}

	pthread_exit(NULL);
}

void *socket_server_fn(void *) {
	pthread_exit(NULL);
}
