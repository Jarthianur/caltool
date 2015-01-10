# Makefile for caltool
#Some compiler stuff and flags
CFLAGS = -g -Wall
LDFLAGS =
EXECUTABLE = caltool
_OBJ = caltool.o fbutils.o font_8x8.o touch.o matrix.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
LIBS = -lncurses -lmenu -ltinfo -linput -ludev
ODIR = obj
BINDIR = /opt/bin

#targets

$(ODIR)/%.o: %.c
	mkdir -p $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)
	
all: caltool

caltool: $(OBJ)
	$(CC) $(LIBS) -g -o $@ $^

clean:
	rm -rf *.o *~ core $(EXECUTABLE)
	
.PHONY: clean all
