CC = gcc
CFLAGS = -Wall -Werror -g

all: myshell

myshell: myshell.c
	$(CC) $(CFLAGS) -o myshell myshell.c

clean:
	rm -f   *.o  *.so myshell
