CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PKGS      = portaudio-2.0 libcurl
CPPFLAGS += -I3rdparty -Isrc $(shell pkg-config --cflags $(PKGS))
LIBS      = $(shell pkg-config --libs $(PKGS)) -pthread

SRCDIR   = src
BUILDDIR = build
TARGET   = meetingrec

SRCS = $(wildcard $(SRCDIR)/*.cpp)
OBJS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean
