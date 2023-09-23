CFLAGS := -Wall -Wextra -std=c99 -Og -g2 -Wconversion -Wdouble-promotion \
     -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion \
     -fsanitize=undefined -fsanitize-trap
all: mupenini2dat
