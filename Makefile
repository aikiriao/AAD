CC 		    = gcc
CFLAGS 	  = -std=c89 -O0 -g3 -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=2 -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
CPPFLAGS	= -DDEBUG
LDFLAGS		= -Wall -Wextra -Wpedantic -O0
LDLIBS		= -lm

SRCS      = aad.c wav.c main.c
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
