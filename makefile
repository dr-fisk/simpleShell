OBJS = sshell.c 

CC = gcc

COMPILER_FLAGS = -Wall -Wextra -Werror -o

OBJ_NAME = sshell

all:
	$(CC) $(OBJS) $(COMPILER_FLAGS) $(OBJ_NAME)

clean:
	rm $(OBJ_NAME)
