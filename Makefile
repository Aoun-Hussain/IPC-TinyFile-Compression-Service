CC = gcc
CFLAGS = -g
LIBS = .
SRC = src/ipc_shared.c
OBJ = $(SRC:.c=.o)

OUT = bin/libtinyfile.a

.c.o:
	$(CC) $(CFLAG) -pthread -c $< -o $@

$(OUT): $(OBJ)
	ar rcs $(OUT) $(OBJ)

all: tinyfile tinyfile_app

tinyfile: src/tinyfile.c
	$(CC) $(CFLAGS) -pthread src/tinyfile.c src/ipc_shared.c src/libsnappy.a -o bin/tinyfile

tinyfile_app: src/tinyfile_app.c
	$(CC) $(CFLAGS) -pthread src/tinyfile_app.c $(OUT) -o bin/tinyfile_app

clean:
	@rm src/*.o bin/tinyfile
	@echo "All files cleaned!"
