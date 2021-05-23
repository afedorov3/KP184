CFLAGS += -Wall -Iinclude
CXXFLAGS += -Wall -Iinclude

DEFINES += -DHAVE_READLINE -DMBDEBUG
ifeq ($(shell uname -o),Cygwin)
    EXESFX = .exe
	LIBS_KP184CMD += -L/usr/bin -lcygreadline7
else
	LIBS_KP184CMD += -lreadline
endif

KP184CMD_OBJS = cmdUI/dev_KP184.opp cmdUI/cmdUI.opp
BATTERY_OBJS = battery.opp
TTY_OBJS = test/tty.opp
LOOP_OBJS = test/loopback.opp

KP184CMD = kp184cmd$(EXESFX)
BATTERY = battery$(EXESFX)
TTY = test/tty$(EXESFX)
LOOP = test/loopback$(EXESFX)

STRIP = strip

all: $(KP184CMD) $(BATTERY)

$(KP184CMD): $(KP184CMD_OBJS) 
	$(CXX) $(LDFLAGS) $(LIBS_KP184CMD) -o $@ $^
	$(STRIP) $@

$(BATTERY): $(BATTERY_OBJS)
	$(CXX) $(LDFLAGS) -lrt $(LIBS_BATTERY) -o $@ $^
	$(STRIP) $@

$(TTY): $(TTY_OBJS)
	$(CXX) $(LDFLAGS) $(LIBS_TTY) -o $@ $^
	$(STRIP) $@

$(LOOP): $(LOOP_OBJS)
	$(CXX) $(LDFLAGS) $(LIBS_LOOP) -o $@ $^
	$(STRIP) $@

cmdUI/cmdUI.opp: cmdUI/cmdUI.cpp cmdUI/device.h include/util.h include/link.h
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ cmdUI/cmdUI.cpp

cmdUI/dev_KP184.opp: cmdUI/dev_KP184.cpp include/util.h include/link.h include/mbrtu.h include/KP184.h
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ cmdUI/dev_KP184.cpp

battery.opp: battery.cpp include/util.h include/link.h include/mbrtu.h include/KP184.h
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ battery.cpp

test/loopback.opp: test/loopback.cpp include/util.h include/link.h include/mbrtu.h
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ test/loopback.cpp

test/tty.opp: test/tty.cpp include/link.h
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ test/tty.cpp

%.o: %.c
	$(CC) -c $(CFLAGS) $(DEFINES) -o $@ $^

%.opp: %.cpp
	$(CXX) -c $(CXXFLAGS) $(DEFINES) -o $@ $^

clean:
	rm -rf $(KP184CMD_OBJS) $(KP184CMD)
	rm -rf $(BATTERY_OBJS) $(BATTERY)
	rm -rf $(TTY_OBJS) $(TTY)
	rm -rf $(LOOP_OBJS) $(LOOP)
