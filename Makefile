# Cross-compiles with a DJGPP toolchain (i586-pc-msdosdjgpp-gcc). If it's
# not on your PATH, e.g. a WSL cross-toolchain under ~/cross/bin, export
# that first: PATH=$HOME/cross/bin:$PATH make
CC = i586-pc-msdosdjgpp-gcc
CFLAGS = -O2 -Wall -Isrc
TARGET = DAGREAD.EXE
OBJS = src/main.o src/pmwin.o src/mouse.o src/dagfile.o src/render.o src/linkpager.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
