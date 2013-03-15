$(shell mkdir -p build)

SRC := $(wildcard src/*.cc)
INCLUDE := $(wildcard */*.h) Makefile
OBJ := $(patsubst src/%.cc,build/%.o,$(SRC))

TEST_SRC := $(wildcard test/*.cc)
TESTS := $(patsubst test/%.cc,build/%,$(TEST_SRC))

CFLAGS := -Wall -O3 -ggdb2 -I/home/power/pkg/openmpi-1.7/include -pthread
CXXFLAGS := $(CFLAGS) -std=c++11 
CXX := g++

LDFLAGS := -pthread -L/home/power/pkg/openmpi-1.7/lib -lmpi_cxx -lmpi -ldl -Wl,--export-dynamic -lrt -lnsl -lutil -lm -ldl

build/% : test/%.cc build/libmpirpc.a
	$(CXX) $(CXXFLAGS) -I. -Isrc $< -o $@ -Lbuild/ $(LDFLAGS) -lmpirpc -lpth

build/%.o : src/%.cc $(INCLUDE)
	$(CXX) $(CXXFLAGS) -I. -Isrc $< -c -o $@

all: $(TESTS)

clean:
	rm -rf build/*

build/libmpirpc.a : $(OBJ) $(INCLUDE)
	ar rcs $@ $^
	ranlib $@
