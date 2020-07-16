#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include "utils.h"

#define MAX_PATH 1024
#define MAX_CHILD_NO 1024
#define MAX_ENTRY_NO 1024
#define PIPE_READ 0
#define PIPE_WRITE 1
#define BUF_SIZE (1024*1024)
#define OK 0

int is_main_program;
pid_t childs_pids[MAX_CHILD_NO];
size_t curr_child_no;
int list_by_name = 0;
int list_by_type = 0;
int list_by_perm = 0;
char *filename;
int list_dir = 1;
int list_reg_file = 1;
int list_symbolic_link = 1;
long int fileperm;
int should_print = 1;
int should_delete = 0;
int should_exec = 0;
char** exec_args;

void kill_childs()
{
	for (size_t i = 0; i < curr_child_no; ++i)
	{
		kill(childs_pids[i], SIGUSR1);
	}
}

void sigint_handler(int signo)
{
	printf("Are you sure you want to terminate (Y/N)?\n");
	char answer = getchar();
	while (1)
	{
		if (answer == 'Y' || answer == 'y')
		{
			kill_childs();
			exit(0);
		}
		else if (answer == 'N' || answer == 'n')
			return;

		if (answer != '\n')
		{
			printf("Please Enter Valid Input:");
		}

		answer = getchar();
	}
}

void sigusr1_handler(int signo)
{
	kill_childs();
	exit(0);
}


void read_subdir(char *dir_path, int pipe_filedes_parent[2])
{
	DIR *dir;
	struct dirent *dentry;
	struct stat stat_entry;
	int childs_pipes[MAX_CHILD_NO];
	char *entry_names[MAX_ENTRY_NO];
	size_t curr_entry_no;
	int entry_is_dir[MAX_ENTRY_NO];
	int entry_is_to_show[MAX_ENTRY_NO];
	int pipe_filedes_child[2];
	memcpy(pipe_filedes_child, pipe_filedes_parent, 2 * sizeof(int)); //At the first child, the child pipe file des must be intialized to the parent file des, so that the code after the next label doens't destroy parent pipe filedes


CHILD_CONTINUE:
	memset(entry_is_dir, 0, MAX_ENTRY_NO);
	memset(entry_is_to_show, 0, MAX_ENTRY_NO);
	memcpy(pipe_filedes_parent, pipe_filedes_child, 2 * sizeof(int));
	curr_child_no = 0;
	curr_entry_no = 0;
	if ((dir = opendir(dir_path)) == NULL)
	{
		perror("\nUnable to open directory.\n");
		exit(0);
	}
	chdir(dir_path);

	char cwd[MAX_PATH];
	getcwd(cwd, sizeof(cwd));

	while ((dentry = readdir(dir)) != NULL)
	{
		stat(dentry->d_name, &stat_entry);
		int statchmod = stat_entry.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
		if (strcmp(dentry->d_name, "..") == 0)
			continue;
		if (strcmp(dentry->d_name, ".") == 0)
			continue;

		if (S_ISDIR(stat_entry.st_mode)) //If the entry is a directory, create a new process to parse it
		{
			//This pipe is used for each child process to pass information to the its parent (instead of print it directly to the screen)
			//this avoids mixing outputs to the screen in a not user friendly way
			if (pipe(pipe_filedes_child) != OK)
			{
				perror("\nUnable to create pipe");
				exit(1);
			}
			pid_t ret;
			ret = fork();
			if (ret == -1)
			{
				perror("\nUnable to create sub process");
				exit(1);
			}
			if (ret != 0)
			{
				close(pipe_filedes_child[PIPE_WRITE]);
				childs_pids[curr_child_no] = ret;
				childs_pipes[curr_child_no] = pipe_filedes_child[PIPE_READ];
				curr_child_no++;
				entry_is_dir[curr_entry_no] = 1; //set the flag to true
				if (list_dir)
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
				else if (list_by_name && (strcmp(dentry->d_name, filename) == 0))
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
				else if (list_by_perm && statchmod == fileperm)
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
			}
			else
			{
				close(pipe_filedes_child[PIPE_READ]);
				curr_child_no = 0;
				strcat(dir_path, "/");
				strcat(dir_path, dentry->d_name);
				goto CHILD_CONTINUE;
			}
		}
		else if (S_ISREG(stat_entry.st_mode) && list_reg_file)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (S_ISLNK(stat_entry.st_mode) && list_symbolic_link)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (list_by_name && (strcmp(dentry->d_name, filename) == 0))
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (list_by_perm && statchmod == fileperm)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}

		char entry_path[MAX_PATH];
		strcpy(entry_path, cwd);
		strcat(entry_path, "/");
		strcat(entry_path, dentry->d_name);
		push_back_str(entry_names, curr_entry_no, entry_path);
		curr_entry_no++;
	}
	char *buf = malloc(BUF_SIZE);
	//Write to the parent pipe the info obtained of the current working directory by this process (non-directorys)
	//Also write to the parent pipe the info obtained from the subfolders bellow the level of the current working directory by the childs (and sub childs and so on) of the process
	//The info is written in an order so that it always prints the folders in subsequent order (like a tree)
	for (size_t i = 0, curr_child = 0; i < curr_entry_no; i++)
	{
		int written;
		if (entry_is_to_show[i])
		{
			written = write(pipe_filedes_parent[PIPE_WRITE], entry_names[i], strlen(entry_names[i]));
			write(pipe_filedes_parent[PIPE_WRITE], "\n", strlen("\n"));
			if (written != strlen(entry_names[i]))
				exit(1);
		}
		if (entry_is_dir[i])
		{
			int term_status;
			int return_value;
			while((return_value = waitpid(childs_pids[i], &term_status, 0)) == -1)
			{
				if(return_value == -1 && errno != EINTR)
					exit(1);
			}
			if (!WIFEXITED(term_status))
				exit(1);

			ssize_t bytes_read;
			//Read each chil pipe on one iteration to get the info of each sub directory of the current working directory (the directory processed by this process)
			while ((bytes_read = read(childs_pipes[curr_child], buf, BUF_SIZE)) != 0)
			{
				if (bytes_read == -1)
					exit(1);
				if (write(pipe_filedes_parent[PIPE_WRITE], buf, bytes_read) != bytes_read)
					exit(1);
			}
			curr_child++;
		}
	}
	close(pipe_filedes_parent[PIPE_WRITE]);
	free(buf);
}

void handle_input_from_user(int argc, char* argv[])
{
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-name") && !list_by_name)
		{
			list_by_name = 1;
			list_by_perm = 0;
			list_by_type = 0;
			list_reg_file = 0;
			list_dir = 0;
			list_symbolic_link = 0;
			if (i + 1 >= argc)
			{
				printf("Invalid arguments. Please try again.\n");
				exit(1);
			}
			filename = argv[++i];
		}
		else if (!strcmp(argv[i], "-type"))
		{
			list_by_type = 1;
			list_by_name = 0;
			list_by_perm = 0;
			if (!strcmp(argv[i + 1], "f"))
			{
				list_reg_file = 1;
				list_dir = 0;
				list_symbolic_link = 0;
				i++;
			}
			else if (!strcmp(argv[i + 1], "d"))
			{
				list_reg_file = 0;
				list_dir = 1;
				list_symbolic_link = 0;
				i++;
			}
			else if (!strcmp(argv[i + 1], "l"))
			{
				list_reg_file = 0;
				list_dir = 0;
				list_symbolic_link = 1;
				i++;
			}
			else
			{
				printf("\nUnsoported Arguments | Suported Arguments: d , f , l\n");
				fflush(stdout);
				exit(-1);
			}
		}
		else if (!strcmp(argv[i], "-perm"))
		{
			list_by_perm = 1;
			list_by_name = 0;
			list_by_type = 0;
			list_reg_file = 0;
			list_dir = 0;
			list_symbolic_link = 0;
			if(i + 1 >= argc)
			{
				printf("Invalid arguments. Please try again.\n");
				exit(1);
			}
			fileperm = strtol(argv[++i], NULL, 8);
		}
		else if (!strcmp(argv[i], "-print"))
		{
			should_print = 1;
		}
		else if (!strcmp(argv[i], "-delete"))
		{
			should_print = 0;
			should_delete = 1;
		}
		else if (!strcmp(argv[i], "-exec"))
		{
			i++;
			should_print = 0;
			should_exec = 1;
			size_t num_args = 0;
			exec_args = malloc(256 * sizeof(char*)); //assuming no more than 256 arguments
			while (i < argc && strcmp(argv[i], ";"))
			{
				push_back_str(exec_args, num_args, argv[i]);
				num_args++;
				i++;
			}
			exec_args[num_args] = NULL;
		}
		else
		{
			printf("\nUnsupported Command: %s\n", argv[i]);
			exit(1);
		}
	}
}

int main(int argc, char* argv[])
{
	is_main_program = 1;
	handle_input_from_user(argc, argv);
	
	//Install handler for SIGINT (only used for the main parent process)
	struct sigaction action;
	action.sa_handler = sigint_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGINT, &action, NULL);
	

	DIR *dir;
	struct dirent *dentry;
	struct stat stat_entry;
	char dir_path[MAX_PATH];
	getcwd(dir_path, sizeof(dir_path));
	int childs_pipes[MAX_CHILD_NO];
	int pipe_filedes[2];
	char *entry_names[MAX_ENTRY_NO];
	size_t curr_entry_no = 0;
	int entry_is_dir[MAX_ENTRY_NO];
	int entry_is_to_show[MAX_ENTRY_NO];

	if ((dir = opendir(dir_path)) == NULL)
	{
		perror("\nUnable to open directory.");
		exit(0);
	}

	char cwd[MAX_PATH];
	getcwd(cwd, sizeof(cwd));

	while ((dentry = readdir(dir)) != NULL)
	{
		stat(dentry->d_name, &stat_entry);
		int statchmod = stat_entry.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
		if (strcmp(dentry->d_name, "..") == 0)
			continue;
		if (strcmp(dentry->d_name, ".") == 0)
			continue;
		if (S_ISDIR(stat_entry.st_mode))
		{
			if (pipe(pipe_filedes) != OK)
			{
				perror("\nUnable to create pipe stuff");
				exit(1);
			}

			pid_t ret = fork();
			if (ret == -1)
			{
				perror("\nUnable to create sub process");
				exit(1);
			}
			if (ret != 0)
			{
				close(pipe_filedes[PIPE_WRITE]);
				childs_pids[curr_child_no] = ret;
				childs_pipes[curr_child_no] = pipe_filedes[PIPE_READ];
				curr_child_no++;
				entry_is_dir[curr_entry_no] = 1;
				if (list_dir)
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
				else if (list_by_name && (strcmp(dentry->d_name, filename) == 0))
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
				else if (list_by_perm && statchmod == fileperm)
				{
					entry_is_to_show[curr_entry_no] = 1;
				}
			}
			else
			{
				sigset_t mask;
				sigemptyset(&mask);
				sigaddset(&mask, SIGINT);
				sigprocmask(SIG_BLOCK, &mask, NULL);
				action.sa_handler = sigusr1_handler;
				sigaction(SIGUSR1, &action, NULL);

				is_main_program = 0;
				close(pipe_filedes[PIPE_READ]);
				strcat(dir_path, "/");
				strcat(dir_path, dentry->d_name);
				//printf("Main program: dir path: %s\n", dir_path);
				read_subdir(dir_path, pipe_filedes);
				return 0;
			}
		}
		else if (S_ISREG(stat_entry.st_mode) && list_reg_file)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (S_ISLNK(stat_entry.st_mode) && list_symbolic_link)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (list_by_name && (strcmp(dentry->d_name, filename) == 0))
		{
			entry_is_to_show[curr_entry_no] = 1;
		}
		else if (list_by_perm && statchmod == fileperm)
		{
			entry_is_to_show[curr_entry_no] = 1;
		}

		char entry_path[MAX_PATH];
		strcpy(entry_path, cwd);
		strcat(entry_path, "/");
		strcat(entry_path, dentry->d_name);
		push_back_str(entry_names, curr_entry_no, entry_path);
		curr_entry_no++;
	}

	char **all_info = malloc(MAX_ENTRY_NO);
	unsigned int all_info_capacity = MAX_ENTRY_NO;
	unsigned int all_info_size = 0;
	memset(all_info, 0, MAX_ENTRY_NO);
	char *buf = malloc(BUF_SIZE);
	for (size_t i = 0, curr_child = 0; i < curr_entry_no; i++)
	{
		if (entry_is_to_show[i])
		{
			push_back_str(all_info, all_info_size, entry_names[i]);
			all_info_size++;
			if (all_info_capacity >= all_info_size)
			{
				all_info_capacity += MAX_ENTRY_NO;
				all_info = realloc(all_info, all_info_capacity);
			}
		}
		if (entry_is_dir[i])
		{
			int term_status;
			int return_value;
			while((return_value = waitpid(childs_pids[i], &term_status, 0)) == -1)
			{
				if(return_value == -1 && errno != EINTR)
					exit(1);
			}
			if (!WIFEXITED(term_status))
				exit(1);
			ssize_t bytes_read;
			char temp[1024];
			while ((bytes_read = read(childs_pipes[curr_child], buf, BUF_SIZE)) != 0)
			{
				if (bytes_read == -1)
					exit(1);
				unsigned int curr_temp_idx = 0;
				for (unsigned int j = 0; j < bytes_read; j++)
				{
					if (buf[j] == '\n')
					{
						temp[curr_temp_idx] = '\0';
						push_back_str(all_info, all_info_size, temp);
						all_info_size++;
						if (all_info_capacity >= all_info_size)
						{
							all_info_capacity += MAX_ENTRY_NO;
							all_info = realloc(all_info, all_info_capacity);
						}
						curr_temp_idx = 0;
					}
					else
					{
						temp[curr_temp_idx] = buf[j];
						curr_temp_idx++;
					}

				}
			}
			curr_child++;
		}
	}


	if (should_print)
	{
		for (size_t i = 0; i < all_info_size; i++)
		{
			if (all_info[i] != NULL)
				printf("%s\n", all_info[i]);
		}
	}

	if (should_exec)
	{
		unsigned int filename_idx;
		for (unsigned int i = 0; exec_args[i] != NULL; i++)
		{
			if (strcmp(exec_args[i], "{}") == 0)
			{
				filename_idx = i;
				break;
			}
		}

		printf("\n\n");
		for (size_t i = 0; i < all_info_size; i++)
		{
			if (all_info[i] != NULL)
			{
				free(exec_args[filename_idx]);
				exec_args[filename_idx] = malloc(sizeof(all_info[i]));
				exec_args[filename_idx] = all_info[i];
				pid_t ret = fork();
				if(ret == -1)
				{
					perror("\nUnable to create a sub-process.\n");
					exit(0);
				}
				if(ret == 0)
				{
					execvp(exec_args[0], exec_args);
				}
				else
				{
					int term_status;
					if(waitpid(ret, &term_status, 0) == -1)
					{
						perror("\nError running exec.\n");
						exit(0);
					}
					else if (!WIFEXITED(term_status))
					{
						perror("\nError executing exec command.\n");
						exit(1);
					}	

					printf("\n\n");
				}
			}
		}
	}

	if (should_delete)
	{
		for (size_t i = 0; i < all_info_size; i++)
		{
			if (all_info[i] != NULL)
				if (unlink(all_info[i]) != OK)
				{
					perror("\nUnable to delete file.\n");
					exit(0);
				}
		}
	}

	closedir(dir);
	return 0;
}
