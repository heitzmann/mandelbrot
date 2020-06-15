SOURCES=$(wildcard *.cpp)
OBJECTS=$(SOURCES:.cpp=.o)
BINS=$(SOURCES:.cpp=)

CXXFLAGS+=-lm -pthread -O3

.PHONY: all clean

all: $(BINS)

clean:
	$(RM) $(OBJECTS) $(BINS)
