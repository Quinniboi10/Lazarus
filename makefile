DEFAULT_NETWORK = Prelude_09.nnue

#Detect Operating System
ifeq ($(OS),Windows_NT)
#Windows settings
    RM := del /F /Q
    EXE_EXT := .exe
else
#Unix / Linux settings
    RM := rm -f
    EXE_EXT :=
endif

#Short commit id of HEAD(thanks to Weiss for this !)
GIT_HEAD_COMMIT_ID_RAW := $(shell git rev-parse --short HEAD)
ifneq ($(GIT_HEAD_COMMIT_ID_RAW),)
GIT_HEAD_COMMIT_ID_DEF := -DGIT_HEAD_COMMIT_ID=\""$(GIT_HEAD_COMMIT_ID_RAW)"\"
else
GIT_HEAD_COMMIT_ID_DEF :=
endif

#Compiler and flags
CXX      := clang++
CXXFLAGS := -O3 -fno-finite-math-only -flto -std=c++20 -DNDEBUG

# Handle ARM vs x86 builds
ifeq ($(OS),Windows_NT)
  ARCH := $(PROCESSOR_ARCHITECTURE)
else
  ARCH := $(shell uname -m)
endif

IS_ARM := $(filter ARM ARM64 arm64 aarch64 arm%,$(ARCH))

ifeq ($(IS_ARM),)
  LINKFLAGS := -fuse-ld=lld
  ARCHFLAGS := -march=native
else
  LINKFLAGS :=
  ARCHFLAGS := -mcpu=native
endif


#Default target executable name and evaluation file path
EXE      ?= Lazarus$(EXE_EXT)
EVALFILE ?= $(DEFAULT_NETWORK)

#Source and object files
SRCS     := $(wildcard ./src/*.cpp)
SRCS     += ./external/fmt/format.cpp
SRCS     += ./external/Pyrrhic/tbprobe.cpp
OBJS     := $(SRCS:.cpp=.o)

# Default target
all: $(EXE)

CXXFLAGS += -MMD -MP

# Build the objects
%.o: %.cpp $(EVALFILE)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) $(GIT_HEAD_COMMIT_ID_DEF) -DEVALFILE="\"$(EVALFILE)\"" -c $< -o $@

DEPS := $(OBJS:.o=.d)
-include $(DEPS)

# Link the executable
$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LINKFLAGS) -o $@

# Net repo URL
NET_BASE_URL := https://git.nocturn9x.space/Quinniboi10/Prelude-Nets/raw/branch/main

# Download if the net is not specified
ifeq ($(EVALFILE),$(DEFAULT_NETWORK))
$(EVALFILE):
	curl -L -o $@ $(NET_BASE_URL)/$@
else
$(EVALFILE):
	echo "Using user-defined NNUE file"
endif

# Files for make clean
CLEAN_STUFF := $(EXE) Lazarus.exp Lazarus.lib Lazarus.pdb $(OBJS) $(DEPS)
ifeq ($(OS),Windows_NT)
    CLEAN_STUFF := $(subst /,\\,$(CLEAN_STUFF))
endif

# Release (static) build
.PHONY: release
release: CXXFLAGS += -static
release: all

# Debug build
.PHONY: debug
debug: CXXFLAGS = -std=c++20 -O2 -ggdb -fno-finite-math-only -fno-inline-functions -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -rdynamic -DBOOST_STACKTRACE_USE_ADDR2LINE -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -Wall -Wextra
debug: all

# Debug build
.PHONY: profile
profile: CXXFLAGS = -std=c++20 -O3 -g -fno-finite-math-only -flto -fno-omit-frame-pointer -DNDEBUG
profile: all

# Force rebuild
.PHONY: force
force: clean
force: all

# Clean up
.PHONY: clean
clean:
	$(RM) $(CLEAN_STUFF)
