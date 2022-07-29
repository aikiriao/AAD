CC 		    = gcc
CFLAGS 	  = -std=c89 -O3 -g3 -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=2 -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
CPPFLAGS	= -DNDEBUG
LDFLAGS		= -Wall -Wextra -Wpedantic -O3
LDLIBS		= -lm

SRCS      = aad_encoder.c aad_decoder.c aad_tables.c wav.c command_line_parser.c main.c
OBJS			= $(SRCS:%.c=%.o)
TARGETS   = aad

all: $(TARGETS) 

rebuild:
	make clean
	make all

clean:
	rm -rf $(TARGETS) $(OBJS)

aad : $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ -c $<
