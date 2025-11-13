# Makefile for STM32 Oscilloscope - Raspberry Pi 4
# Usage:
#   make        - Build the project
#   make clean  - Remove build files
#   make run    - Build and run
#   make install - Install to /usr/local/bin

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -fPIC
TARGET = scope
SOURCE = pi4.cpp

# Qt5 flags
QT_CFLAGS = $(shell pkg-config --cflags Qt5Widgets)
QT_LIBS = $(shell pkg-config --libs Qt5Widgets)

# MOC file (Qt Meta-Object Compiler)
MOC_FILE = main.moc

.PHONY: all clean run install help check

all: check $(TARGET)

$(TARGET): $(SOURCE)
	@echo "🔨 Compiling $(SOURCE)..."
	$(CXX) -o $(TARGET) $(SOURCE) $(CXXFLAGS) $(QT_CFLAGS) $(QT_LIBS)
	@echo "✅ Build complete: $(TARGET)"
	@echo ""

check:
	@echo "🔍 Checking dependencies..."
	@which $(CXX) > /dev/null || (echo "❌ g++ not found"; exit 1)
	@pkg-config --exists Qt5Widgets || (echo "❌ Qt5Widgets not found"; exit 1)
	@echo "✅ All dependencies OK"
	@echo ""

clean:
	@echo "🧹 Cleaning build files..."
	rm -f $(TARGET) $(MOC_FILE)
	@echo "✅ Clean complete"

run: $(TARGET)
	@echo "▶️  Running $(TARGET)..."
	./$(TARGET)

install: $(TARGET)
	@echo "📦 Installing to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "✅ Installed. Run with: scope"

help:
	@echo "STM32 Oscilloscope - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the oscilloscope app"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make run      - Build and run"
	@echo "  make install  - Install to /usr/local/bin"
	@echo "  make check    - Check dependencies"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Requirements:"
	@echo "  - g++ with C++17 support"
	@echo "  - Qt5 (Qt5Widgets)"
	@echo "  - SPI enabled on Raspberry Pi"
	@echo ""
