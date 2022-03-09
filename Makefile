CXXFLAGS = -std=c++2a -g
LDFLAGS = -pthread
DEPS     = udp_util.h 

all: udp_client udp_server 

udp_server: $(DEPS)

udp_client: $(DEPS)

clean:
	rm -f udp_client udp_server *.dat
