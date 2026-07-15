#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define GPIO_IMPLEMENTATION
#include "../blink/gpio.h"
#define THERMSENSE_IMPLEMENTATION
#include "thermsense.h"

extern int sigint_handler_gpiofd;
void sigint_handler(int);

int main(void) {
	/** SENSORS **/

	sensor_name_t sensors[16];
	size_t sensors_len = get_sensors(sensors, sizeof(sensors)/sizeof(*sensors));

	puts("Found sensors:");
	for (size_t i = 0; i < sensors_len; ++i) {
		printf(" - %s\n", sensors[i]);
	}

	puts("");

	puts("Temperatures:");
	for (size_t i = 0; i < sensors_len; ++i) {
		printf(" - %f\n", get_temperature(sensors[i]));
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
		sleep(1);

		bool state = false;
		puts("Checking if LED should be on...");
		for (size_t i = 0; i < sensors_len; ++i) {
			const float temp = get_temperature(sensors[i]);
			if (temp >= 20) {
				printf("  Sensor %s has temp %f!\n", sensors[i], temp);
				state = !state;
			}
		}
		printf("LED should be %s.\n", state ? "on" : "off");

		set_gpio_pin(ledfd, state);
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
