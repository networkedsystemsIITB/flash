#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cjson/cJSON.h"

// Function to read file contents into a string
char *read_file(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (!file) {
		perror("Failed to open file");
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	rewind(file);

	char *data = (char *)malloc(length + 1);
	if (!data) {
		perror("Memory allocation failed");
		fclose(file);
		return NULL;
	}

	fread(data, 1, length, file);
	data[length] = '\0'; // Null-terminate the string

	fclose(file);
	return data;
}

int main()
{
	const char *filename = "config.json";

	// Read JSON file into a string
	char *json_string = read_file(filename);
	if (!json_string)
		return 1;

	// Parse JSON
	cJSON *json = cJSON_Parse(json_string);
	if (!json) {
		printf("JSON parsing failed!\n");
		free(json_string);
		return 1;
	}

	// Extract num_backends
	cJSON *num_backends = cJSON_GetObjectItem(json, "num_backends");
	if (cJSON_IsNumber(num_backends)) {
		printf("Number of Backends: %d\n", num_backends->valueint);
	}

	// Extract backend addresses array
	cJSON *bkd_addresses = cJSON_GetObjectItem(json, "bkd_addresses");
	if (cJSON_IsArray(bkd_addresses)) {
		int size = cJSON_GetArraySize(bkd_addresses);
		printf("Backend Addresses:\n");
		for (int i = 0; i < size; i++) {
			cJSON *addr = cJSON_GetArrayItem(bkd_addresses, i);
			if (cJSON_IsString(addr)) {
				printf("  %s\n", addr->valuestring);
			}
		}
	}

	// Cleanup
	cJSON_Delete(json);
	free(json_string);
	return 0;
}
