/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "exec_parser.h"

static so_exec_t *exec;
static struct sigaction old_hand;
int fd;

static void segfault(int num, siginfo_t *info, void *data)
{
	so_seg_t *curr_seg = NULL;
	int page_size = getpagesize();
	int flags;
	int zone_to_zero = 0;
	for (int i = 0; i < exec->segments_no && !curr_seg; i++)
		if ((uintptr_t)info->si_addr >= exec->segments[i].vaddr && (uintptr_t)info->si_addr < exec->segments[i].vaddr + exec->segments[i].mem_size)
			curr_seg = &(exec->segments[i]);
	if (!curr_seg)
		old_hand.sa_sigaction(num, info, data);
	else
	{
		unsigned char *info_about_map = (unsigned char *)curr_seg->data;
		uintptr_t curr_offset_in_page = (uintptr_t)info->si_addr - curr_seg->vaddr;
		int page = curr_offset_in_page / page_size;
		if (info_about_map[page] == 1)
			old_hand.sa_sigaction(num, info, data);
		else
		{
			flags = MAP_PRIVATE | MAP_FIXED;
			void *res = mmap((void *)(curr_seg->vaddr + page * page_size), page_size, curr_seg->perm, flags, fd, curr_seg->offset + page * page_size);
			if (res == MAP_FAILED)
				exit(-ENOMEM);
			if (curr_seg->file_size < curr_seg->mem_size && (page + 1) * page_size > curr_seg->file_size)
			{
				zone_to_zero = (page + 1) * page_size - curr_seg->file_size;
				uintptr_t aux = curr_seg->vaddr + (page + 1) * page_size - zone_to_zero;
				if (zone_to_zero)
					memset((void *)aux, 0, zone_to_zero);
			}
			info_about_map[page] = 1;
		}
	}
}

int so_init_loader(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_flags |= SA_SIGINFO;
	sigaddset(&action.sa_mask, SIGSEGV);
	action.sa_sigaction = segfault;
	sigaction(SIGSEGV, &action, &old_hand);
	return -1;
}

int so_execute(char *path, char *argv[])
{
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	exec = so_parse_exec(path);
	if (!exec)
		return -1;
	int page_size = getpagesize();
	for (int i = 0; i < exec->segments_no; i++)
	{
		int number_pages = (exec->segments[i].mem_size % page_size) ? exec->segments[i].mem_size / page_size + 1 : exec->segments[i].mem_size / page_size;
		unsigned char *map_flag = calloc(number_pages, sizeof(unsigned char));
		exec->segments[i].data = map_flag;
	}
	so_start_exec(exec, argv);
	for (int i = 0; i < exec->segments_no; i++)
	{
		int number_pages = (exec->segments[i].mem_size % page_size) ? exec->segments[i].mem_size / page_size + 1 : exec->segments[i].mem_size / page_size;
		unsigned char *map_flag = exec->segments[i].data;
		for (int j = 0; j < number_pages; j++)
			if (map_flag[j] == 1)
				munmap((void *)(exec->segments[i].vaddr + j * page_size), page_size);
		free(exec->segments[i].data);
	}
	return -1;
}
