AUTOMAKE_OPTIONS = no-dist
INCLUDES = -I$(top_srcdir)/include
bin_PROGRAMS = gputrans
gputrans_SOURCES = main.c render_child.c queue.c logging.c video_in.c
gputrans_LDADD = -lavformat_pic -lavcodec_pic -lavutil_pic -lvorbis -lvorbisenc -ltheora -logg -ldts_pic -la52 -lraw1394 -ldc1394_control -lgsm -lz -lm
AM_CFLAGS = -g -O6 -Wall -Werror -std=c99 -pedantic
