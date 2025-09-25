# Makefile for Matching Engine

CXX = g++
TARGET = matching_engine
SOURCES = main.cpp order_book.cpp market_data.cpp

CXXFLAGS = -std=c++17 -Wall -O3 -march=native -DNDEBUG -flto

# Default: build the matching engine
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

# Run demo
run: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET) *.csv *.o