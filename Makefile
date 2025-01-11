ifdef DEBUG
	CFLAGS=-g -DDEBUG
else
	CFLAGS=-g
endif

LDFLAGS=-lc

SOURCES=service.c
OBJECTS=$(SOURCES:%.c=%.o)
TARGET=mephisto

%.o: %.c
	gcc -c -o $@ $(CFLAGS) $<

$(TARGET): $(OBJECTS)
	gcc -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean $(TARGET)