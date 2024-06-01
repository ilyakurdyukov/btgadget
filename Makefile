
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
APPNAME = btgadget
#LIBS = -lbluetooth

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): $(APPNAME).c tjd.h atorch.h moyoung.h
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)
