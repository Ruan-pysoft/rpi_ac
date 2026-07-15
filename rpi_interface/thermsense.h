#ifndef THERMSENSE_H
#define THERMSENSE_H

#include <stddef.h>

typedef char sensor_name_t[16];

size_t get_sensors(sensor_name_t *sensors_buf, size_t buf_len);
float get_temperature(const sensor_name_t sensor);

#endif // THERMSENSE_H

#ifdef THERMSENSE_IMPLEMENTATION

#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

size_t get_sensors(sensor_name_t *sensors_buf, size_t buf_len) {
	DIR *sensors_dir = opendir("/sys/bus/w1/devices");
	if (sensors_dir == NULL) {
		perror("couldn't open directory");
		exit(1);
	}

	size_t n_sensors = 0;

	struct dirent *entry;
	while ((entry = readdir(sensors_dir)) != NULL) {
		if (n_sensors >= buf_len) break;

		// printf("ENTRY: %s\n", entry->d_name);
		const size_t entry_len = strnlen(entry->d_name, 16);
		if (entry_len != 15) continue;

		if (entry->d_name[0] != '2') continue;
		if (entry->d_name[1] != '8') continue;
		if (entry->d_name[2] != '-') continue;

		memcpy(sensors_buf[n_sensors++], entry->d_name, 16);
	}

	return n_sensors;
}

float get_temperature(const sensor_name_t sensor) {
	assert(sensor[0] == '2');
	assert(sensor[1] == '8');
	assert(sensor[2] == '-');

	static const char prefix[] = "/sys/bus/w1/devices/";
	static const size_t prefix_len = sizeof(prefix)/sizeof(*prefix) - 1;
	char *temp_fname = strdup("/sys/bus/w1/devices/28-012345678901/temperature");
	assert(temp_fname != NULL);

	char temp_buf[16] = {0};
	size_t temp_len = 0;

	memcpy(&temp_fname[prefix_len], sensor, 15);

	FILE *temp_file = fopen(temp_fname, "rw");
	free(temp_fname);
	if (temp_file == NULL) {
		perror("couldn't open temperature file");
		exit(1);
	}

	temp_len = fread(temp_buf, 1, sizeof(temp_buf)/sizeof(*temp_buf) - 1, temp_file);
	if (temp_len == 0) {
		fclose(temp_file);

		perror("couldn't read temperature");
		exit(1);
	}

	fclose(temp_file);

	int temp = atoi(temp_buf);
	return temp / 1000.f;
}

#endif // THERMSENSE_IMPLEMENTATION
