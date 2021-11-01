TARGET           = test
INC              = ../include
LIB              = ..

IGCC            ?= xcrun -sdk iphoneos clang -arch arm64 -arch arm64e
IGCC_FLAGS      ?= -Wall -O3 -I$(INC) -L$(LIB) -lkrw
SIGN            ?= codesign
SIGN_FLAGS      ?= -s - --entitlements ent.plist

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(IGCC) $(IGCC_FLAGS) -o $@ $^
	$(SIGN) $(SIGN_FLAGS) $@

clean:
	rm -f $(TARGET)
