.PHONY: all clean
TARGETS := vm.0.out vm.1.out vm.2.out
WARN_FLAGS := -Wall -Wpedantic
CXX_FLAGS := -g -O3 -std=c++20
# This was tested against clang 14.0.0
# The optimizations applied may be different in other versions,
# which would affect results
CXX := clang-14

all: $(TARGETS)
clean:
	rm -rf $(TARGETS)

vm.0.out: vm.cpp
	$(CXX) -DSPEC=0 $(CXX_FLAGS) $(WARN_FLAGS) -o $@ $^

# LTO is purely to remove the empty "dummy" function
vm.1.out: vm.cpp dummy.cpp
	$(CXX) -DSPEC=1 -flto=full $(CXX_FLAGS) $(WARN_FLAGS) -o $@ $^

vm.2.out: vm.cpp dummy.cpp
	$(CXX) -DSPEC=2 -flto=full $(CXX_FLAGS) $(WARN_FLAGS) -o $@ $^
