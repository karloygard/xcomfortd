LIBS = -lusb-1.0 -lmosquitto
CFLAGS = -Wall -g -std=c++11
CXXFLAGS = -Wall -g -std=c++11
LDFLAGS = -g

default: xcomfortd # test

%.o: %.c
	$(CXX) $(CFLAGS) -c $< -o $@

xcomfortd: ckoz0014.o usb.o main.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

test: ckoz0013/ckoz0013.o ckoz0013/lib_crc.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -rf xcomfortd *.o
