CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
TARGET  = ptp
PREFIX ?= /usr/local

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
