MAIN=mandelbrot
SOURCES=$(wildcard *.cpp)
OBJECTS=$(SOURCES:.cpp=.o)

EXAMPLES=examples.png
EXAMPLE_SRCS=$(shell bash -c 'seq -f "example-%g.thumb.png" -s " " 21')

CXXFLAGS+=-lm -pthread -O2

.PHONY: all clean

all: $(MAIN)

clean:
	$(RM) $(OBJECTS) $(MAIN)

$(EXAMPLES): $(EXAMPLE_SRCS)
	montage -geometry 480x270+4+4 -tile 3x7 $^ $@
	rm $^

%.thumb.png: $(MAIN)
	./$(MAIN) -g 960 540 -r $(shell bash -c 'echo $$RANDOM') $@
