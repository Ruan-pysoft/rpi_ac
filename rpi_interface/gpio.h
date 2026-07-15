#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

int get_gpio_pin_output(int chipfd, int pin_num);
int set_gpio_pin(int pinfd, bool state);

#endif // GPIO_H

#ifdef GPIO_IMPLEMENTATION

#include <linux/gpio.h>
#include <sys/ioctl.h>

int get_gpio_pin_output(int chipfd, int pin_num) {
	struct gpio_v2_line_config config = {
		.flags = GPIO_V2_LINE_FLAG_OUTPUT,
		.num_attrs = 0,
		.padding = {0},
		.attrs = {0},
	};
	struct gpio_v2_line_request req = {
		.offsets = {pin_num},
		.consumer = "blink.c",
		.config = config,
		.num_lines = 1,
		.event_buffer_size = 0, // idc
		.padding = {0},
		.fd = -1,
	};

	int res = ioctl(chipfd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (res == -1) return -1;
	return req.fd;
}

int get_gpio_pin_output_inverted(int chipfd, int pin_num) {
	struct gpio_v2_line_config config = {
		.flags = GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_ACTIVE_LOW,
		.num_attrs = 0,
		.padding = {0},
		.attrs = {0},
	};
	struct gpio_v2_line_request req = {
		.offsets = {pin_num},
		.consumer = "blink.c",
		.config = config,
		.num_lines = 1,
		.event_buffer_size = 0, // idc
		.padding = {0},
		.fd = -1,
	};

	int res = ioctl(chipfd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (res == -1) return -1;
	return req.fd;
}

int set_gpio_pin(int pinfd, bool state) {
	struct gpio_v2_line_values values = {
		.bits = (state << 0),
		.mask = (1 << 0),
	};
	return ioctl(pinfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
}

#endif // GPIO_IMPLEMENTATION
