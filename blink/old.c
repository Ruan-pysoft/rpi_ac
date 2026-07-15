#include <errno.h>
#include <fnctl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096

// Register offsets (relative to /dev/gpiomem mapping)
#define GPFSEL0 0x00   // Function select 0
#define GPSET0  0x1C   // Pin output set 0
#define GPCLR0  0x28   // Pin output clear 0
#define GPLEV0  0x34   // Pin level 0

static volatile uint32_t *gpio;

static void panic(const char *msg) {
	perror(msg);
	exit(1);
}

static void set_mode_output(unsigned int pin) {
	// GPFSEL registers: each pin uses 3 bits.
	// pin 0..9 in GPFSEL0 (0x00), 10..19 in GPFSEL1, etc.
	unsigned fsel_reg = pin / 10;              // which GPFSELn
	unsigned fsel_off = (pin % 10) * 3;       // which 3-bit field
	volatile uint32_t *fsel = (volatile uint32_t *)((char *)gpio + GPFSEL0) + fsel_reg;

	uint32_t val = *fsel;
	val &= ~(7u << fsel_off);                 // clear 3-bit field
	val |=  (1u << fsel_off);                // 001 = output
	*fsel = val;
}

static void write_pin(unsigned int pin, bool high) {
	unsigned int setclr_reg = pin / 32;
	unsigned int bit = pin % 32;

	volatile uint32_t *set = (volatile uint32_t *)((char*)gpio + GPSET0) + setclr_reg;
	volatile uint32_t *clr = (volatile uint32_t *)((char*)gpio + GPCLR0) + setclr_reg;

	if (high) *set = (1u << bit);
	else      *clr = (1u << bit);
}

int main(void) {
	unsigned int pin = 0;
	bool high = false;

	int fd = open("/dev/gpiomem", O_RDWR);
	if (fd < 0) panic("open(/dev/gpiomem)");

	gpio = (volatile uint32_t *)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if (gpio == MAP_FAILED) panic("mmap");

	set_mode_output(pin);
	for (int i = 0; i < 10; ++i) {
		sleep(1);
		write_pin(pin, high);
		high = !high;
	}

	write_pin(pin, false);

	munmap((void*)gpio, PAGE_SIZE);

	return 0;
}
