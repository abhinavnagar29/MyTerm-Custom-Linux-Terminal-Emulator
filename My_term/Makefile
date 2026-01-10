.PHONY: all clean run

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g `pkg-config --cflags x11 cairo pangocairo`
LDFLAGS = `pkg-config --libs x11 cairo pangocairo`

SRC = \
	myterm/utils.c \
	myterm/gui.c \
	myterm/input.c \
	myterm/history.c \
	myterm/multiwatch.c \
	myterm/tabs.c \
	myterm/shell.c \
	myterm/main.c

OBJ = $(SRC:.c=.o)
BIN = myterm/myterm

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

myterm/%.o: myterm/%.c myterm/myterm.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
