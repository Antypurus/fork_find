#include "utils.h"
#include <string.h>
#include <stdio.h>

void push_back_str(char **arr, size_t idx, char *str)
{
	size_t len = strlen(str);
	arr[idx] = malloc(len * sizeof(char) + 1);
	if (arr[idx] == NULL)
	{
		perror("Failed to allocate memory");
		exit(1);
	}
	memcpy(arr[idx], str, len + 1);
}

void add_new_line_at(char **arr, size_t idx)
{
	arr[idx] = realloc(arr[idx], sizeof(arr[idx]) + strlen("\n"));
	if (arr[idx] == NULL)
	{
		perror("Failed to allocate memory");
		exit(1);
	}
	strcat(arr[idx], "\n");
}

void push_back_buf(char **arr, size_t idx, char *buf, size_t buf_size)
{
	arr[idx] = malloc(buf_size);
	if (arr[idx] == NULL)
	{
		perror("Failed to allocate memory");
		exit(1);
	}
	memcpy(arr[idx], buf, buf_size);
}
