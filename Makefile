CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17
TARGET := FINDSCC
SOURCES := \
	source/baseline.cpp \
	source/commonfunctions.cpp \
	source/main.cpp \
	source/online_search.cpp \
	source/optimized.cpp \
	source/temporal_graph.cpp

.PHONY: all clean amazon

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -Isource $(SOURCES) -o $(TARGET)

amazon: amazon_handler.cpp
	$(CXX) $(CXXFLAGS) amazon_handler.cpp -o amazon_handler

clean:
	$(RM) $(TARGET) $(TARGET).exe amazon_handler amazon_handler.exe
