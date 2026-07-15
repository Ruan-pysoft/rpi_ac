#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define GPIO_IMPLEMENTATION
#include "../blink/gpio.h"
#define THERMSENSE_IMPLEMENTATION
#include "../thermtest/thermsense.h"

extern int sigint_handler_gpiofd;
void sigint_handler(int);

const char *kantoor_id = "28-0516a35791ff";
const char *dak_id = "28-0516a43debff";

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

	/** EVENTLOOP **/

	for (;;) {
		puts("Checking if FAN should be on...");

		const float kantoor_temp = get_temperature(kantoor_id);
		printf("  Sensor [kantoor] has temp %f!\n", kantoor_temp);
		const float dak_temp = get_temperature(dak_id);
		printf("  Sensor [dak] has temp %f!\n", dak_temp);

		const bool enable = dak_temp >= kantoor_temp + 5;
		printf("FAN should be %s.\n", enable ? "on" : "off");

		set_gpio_pin(fanfd, enable);

		sleep(5 * 60);
	}

	set_gpio_pin(fanfd, false);
	close(fanfd);
}

int sigint_handler_gpiofd;
void sigint_handler(int) {
	puts("Quitting!");
	puts("putting FAN off...");

	set_gpio_pin(sigint_handler_gpiofd, false);
	_exit(1);
}
