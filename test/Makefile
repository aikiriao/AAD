CC 		    = gcc
CFLAGS 	  = -std=c89 -O0 -g3 -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=2 -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
CPPFLAGS	= -DDEBUG
LDFLAGS		=
LDLIBS    = -lm
SRC				= test_main.c test.c test_byte_array.c test_aad_encoder.c test_aad_decoder.c test_aad_tables.c test_aad_encode_decode.c
INCLUDE   = 
OBJS	 		= $(SRC:%.c=%.o) 
TARGET    = test 

all: $(TARGET) 

rebuild:
	make clean
	make all

run: $(TARGET)
	./test

clean:
	rm -f $(OBJS) $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(TARGET)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -c $<
