
# Copyright 2020 Taras Zakharchenko
# Copyright 2026 Pitriss

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1.  Redistributions of source code must retain the above copyright notice, this
#     list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright notice,
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
# 3.  Neither the name of the copyright holder nor the names of its contributors
#     may be used to endorse or promote products derived from this software without
#     specific prior written permission.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Out-of-tree CLI build. The root Makefile delegates to this file.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC := $(ROOT)/cli
BUILD := $(ROOT)/build/cli
TARGET := $(BUILD)/encoder
OBJECTS := $(BUILD)/encoder.o $(BUILD)/keyboard_control.o
PREFIX ?= /usr
CFLAGS ?= -O2

.PHONY: all install clean

all: $(TARGET)

$(BUILD):
	mkdir -p "$(BUILD)"

$(BUILD)/%.o: $(SRC)/%.c $(SRC)/keyboard_control.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o "$@" "$<"

$(TARGET): $(OBJECTS)
	$(CC) -o "$@" $(OBJECTS) $(LDFLAGS)

install: all
	install -Dm755 "$(TARGET)" "$(DESTDIR)$(PREFIX)/bin/encoder"
	install -Dm644 "$(ROOT)/docs/man/man1/encoder.1" "$(DESTDIR)$(PREFIX)/share/man/man1/encoder.1"

clean:
	rm -rf "$(BUILD)"
