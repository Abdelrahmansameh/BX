BXPATH := /usr/local/gcc-9.2.0/bin:/usr/local/cmake-3.14.0/bin:/usr/local/bin:/usr/bin
CC := $(shell PATH=$(BXPATH) which gcc)
CXX := $(shell PATH=$(BXPATH) which g++)

TARGET := bx.exe
REGRESSION_DIR := regression_tests

.PHONY: all clean spotless debug

$(TARGET): $(wildcard *.h *.cpp *.g4)
	mkdir -p build && cd build && PATH=$(BXPATH) CC=$(CC) CXX=$(CXX) cmake .. && make -j4
	rm -f $(TARGET) && cp -a build/$(TARGET) .
	@touch $(TARGET)

debug:
	@echo CC $(CC)
	@echo CXX $(CXX)

clean:
	rm -f $(TARGET)

spotless: clean
	rm -rf build
	rm -f $(filter-out $(wildcard $(REGRESSION_DIR)/*.bx),$(wildcard $(REGRESSION_DIR)/*))

### Change this to point to wherever you placed the BX interpreter
BX_INTERPRETER := /users/profs/info/kaustuv.chaudhuri/CSE302/BX2/interpret.exe

.PHONY: tests
tests: $(TARGET)
	for f in $(wildcard regression_tests/*.bx) ; do \
	  build/$(TARGET) $$f ; \
	  $${f%bx}exe > $${f%bx}actual ; \
	  $(BX_INTERPRETER) $$f > $${f%bx}expected ; \
	  diff $${f%bx}expected $${f%bx}actual ; \
	  if test $$? -ne 0 ; then \
	    echo Test $$f failed ; \
	    exit 255 ; \
	  fi \
	done
