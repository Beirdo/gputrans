AUTOMAKE_OPTIONS = no-dist
INCLUDES = -I$(top_srcdir)/include
AM_CFLAGS = -g -O6 -Wall -Werror -std=c99 -pedantic

bin_PROGRAMS = gputrans test_decode
gputrans_SOURCES = main.c render_child.c ipc_queue.c ipc_logging.c video_in.c
test_decode_SOURCES = test_decode.c ipc_queue.c ipc_logging.c video_in.c
