# ============================================================
# Makefile - CT-353 CCP Project (Linux)
# Build: make
# Run : ./fft_bass input.wav timestamps.txt
# ============================================================

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = fft_bass
SRCS     = main.cpp file_io.cpp fft.cpp parallel.cpp ipc.cpp peak.cpp
OBJS     = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
