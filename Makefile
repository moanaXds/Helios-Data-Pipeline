CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -Wno-deprecated-declarations

BUILD_DIR = build
SRC_DIR   = src

# Detect OS: add -lrt only on Linux (not needed on macOS)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    RT_LIB = -lrt
else
    RT_LIB =
endif

all: $(BUILD_DIR) dispatcher ingester processor reporter

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

dispatcher:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/dispatcher.cpp \
		$(RT_LIB) -o $(BUILD_DIR)/dispatcher

ingester:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/ingester.cpp \
		-o $(BUILD_DIR)/ingester

processor:
	$(CXX) $(CXXFLAGS) -pthread $(SRC_DIR)/processor.cpp \
		$(RT_LIB) -o $(BUILD_DIR)/processor

reporter:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/reporter.cpp \
		$(RT_LIB) -o $(BUILD_DIR)/reporter

clean:
	rm -rf $(BUILD_DIR)/*