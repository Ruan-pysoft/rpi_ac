#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GPIO_IMPLEMENTATION
#include "gpio.h"

void fsleep(float secs) {
	usleep(secs * 1000 * 1000);
}

#define panic(msg, label) do { _res = 1; perror(msg); goto label; } while (0)

int sigint_handler_gpiofd;
void sigint_handler(int) {
	puts("putting LED off...");

	/*bool on = false;
	for (int i = 0; i < 30; ++i) {
		set_gpio_pin(sigint_handler_gpiofd, on);
		on = !on;
		fsleep(1 / 20.f);
	}*/

	set_gpio_pin(sigint_handler_gpiofd, false);
	_exit(1);
}

int main() {
	int _res = 0;

	int chipfd = open("/dev/gpiochip0", O_RDWR);
	if (chipfd == -1) panic("open(/dev/gpiochip0)", finish);

	int gpiofd = get_gpio_pin_output(chipfd, 26);
	if (gpiofd == -1) panic("fetching gpio pin", finish);

	if (set_gpio_pin(gpiofd, false) == -1) panic("setting gpio pin state", finish_gpiofd);

	sigint_handler_gpiofd = gpiofd;
	if (signal(SIGINT, sigint_handler) == SIG_ERR) panic("setting up signal handler", finish_disable_pin);

	bool on = true;
	for (;;) {
		if (set_gpio_pin(gpiofd, on) == -1) panic("setting gpio pin state", finish_disable_pin);
		on = !on;
		//fsleep(0.25);
		sleep(5);
	}

finish_disable_pin:
	if (set_gpio_pin(gpiofd, false) == -1) panic("setting gpio pin state", finish_gpiofd);
finish_gpiofd:
	close(gpiofd);
finish:
	return _res;
}
