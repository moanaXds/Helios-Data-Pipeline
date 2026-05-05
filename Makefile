CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

BUILD_DIR = build
SRC_DIR = src

all: $(BUILD_DIR) dispatcher ingester processor reporter

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

dispatcher:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/dispatcher.cpp -o $(BUILD_DIR)/dispatcher

ingester:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/ingester.cpp -o $(BUILD_DIR)/ingester

processor:
    $(CXX) $(CXXFLAGS) -pthread $(SRC_DIR)/processor.cpp -o $(BUILD_DIR)/processor

reporter:
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/reporter.cpp -o $(BUILD_DIR)/reporter

clean:
	rm -rf $(BUILD_DIR)/*