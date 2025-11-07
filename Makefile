
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic
APPNAME = btgadget
#LIBS = -lbluetooth

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): tjd.h atorch.h moyoung.h uuid_info.h yhk_print.h
$(APPNAME): $(APPNAME).c
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)
