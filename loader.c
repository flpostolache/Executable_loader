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
	int i;
	uintptr_t converted_addr = (uintptr_t)info->si_addr;

	/* Search the segment in which the Page Fault happened. */
	for (i = 0; i < exec->segments_no && !curr_seg; i++)
		/* If the address is between the start of the segment and the */
		/* end of it, stop searching and get the info for the */
		/* current segment. */
		if (converted_addr >= exec->segments[i].vaddr && converted_addr < exec->segments[i].vaddr + exec->segments[i].mem_size)
			curr_seg = &(exec->segments[i]);

	/* If the search finished and the curr_seg variable is still NULL, */
	/* that means that it was an invalid access. Run the old handler. */
	if (!curr_seg)
		old_hand.sa_sigaction(num, info, data);
	else {
		/* Else, calculate the page of the segment in which the */
		/* Page Fault occurred. */
		unsigned char *info_about_map = (unsigned char *)curr_seg->data;
		int curr_offset_in_seg = converted_addr - curr_seg->vaddr;
		int page = curr_offset_in_seg / page_size;

		/* If the page was already mapped, that means that it is an access */
		/* with the wrong permissions. Run the old handler. */
		if (info_about_map[page] == 1)
			old_hand.sa_sigaction(num, info, data);
		else {
			/* Else, we need to map a new page and copy the data in it */
			void *start_of_page = (void *)curr_seg->vaddr + page * page_size;
			void *res = mmap(start_of_page, page_size, curr_seg->perm, MAP_PRIVATE | MAP_FIXED, fd, curr_seg->offset + page * page_size);

			if (res == MAP_FAILED)
				return;
			int start_page_to_zero = curr_seg->file_size / page_size;

			/* If the file size of the segment is smaller than the file size */
			/* that should be in the memory that means that we need to */
			/* initialize that data with 0. Check also that the Page Fault */
			/* happened in a page that we need to initialize with 0. */
			if (curr_seg->mem_size > curr_seg->file_size && page >= start_page_to_zero && curr_offset_in_seg <= curr_seg->mem_size) {
				/* If the Page Fault happened in the same page as the */
				/* start of the zero zone starts, we need to be careful */
				/* and initialize less data with zero so that we do not */
				/* erase other data. */
				if (page == start_page_to_zero)
					memset((void *)(curr_seg->vaddr + curr_seg->file_size), 0, page_size - curr_seg->file_size % page_size);
				else
				/* Else, just zero all the data in the page. */
					memset(start_of_page, 0, page_size);
			}
			info_about_map[page] = 1;
		}
	}
}

int so_init_loader(void)
{
	/* Create the custom SIGSEGV custom handler. */
	struct sigaction action;
	/* Set everything to zero. */
	memset(&action, 0, sizeof(struct sigaction));
	/* We want a handler with more info. */
	action.sa_flags |= SA_SIGINFO;
	/* Attach the new handler function. */
	action.sa_sigaction = segfault;
	/* Replace the old SIGSEGV handler with the new one. */
	sigaction(SIGSEGV, &action, &old_hand);
	return -1;
}

int so_execute(char *path, char *argv[])
{
	/* Open the file that contains the executable to know what to copy. */
	fd = open(path, O_RDONLY);
	/* Parse the ELF file given by path. */
	exec = so_parse_exec(path);
	if (!exec)
		return -1;
	
	/* Get the page size */
	int page_size = getpagesize();

	/* Iterate through all segments and attach to each one a helper vector that tells*/
	/* if a page from that segment has already been mapped or not. */
	for (int i = 0; i < exec->segments_no; i++) {
		int number_pages = (exec->segments[i].mem_size % page_size) ? exec->segments[i].mem_size / page_size + 1 : exec->segments[i].mem_size / page_size;
		unsigned char *map_flag = calloc(number_pages, sizeof(unsigned char));

		exec->segments[i].data = map_flag;
	}

	/* Start the executable. */
	so_start_exec(exec, argv);
	return -1;
}
