CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -I src

# Source files for the core logic and tests
SRCS = tests/test_protocol.cpp \
       tests/test_main_mocks.cpp \
       src/protocol/parser.cpp \
       src/protocol/resp_parser.cpp \
       src/storage/kv_store.cpp \
       src/server/commands.cpp

# Output binary
TARGET = test_protocol

# Build rule
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

# Clean rule
clean:
	rm -f $(TARGET)

# Run tests
test: $(TARGET)
	./$(TARGET)

.PHONY: clean test
