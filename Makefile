TARGET = otp

CXX = g++

CXXFLAGS = -Wall -Wextra -g -O2 -std=c++17

LDFLAGS = -pthread

SRCS = otp.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
