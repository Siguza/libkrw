TARGET  = libkrw
SRC     = src
INC     = include

IGCC        ?= xcrun -sdk iphoneos clang -arch arm64 -arch arm64e
IGCC_FLAGS  ?= -Wall -O3 -I$(INC)
DYLIB_FLAGS ?= -shared -Wl,-install_name,/usr/lib/$(TARGET).dylib
SIGN        ?= codesign
SIGN_FLAGS  ?= -s -
TAPI        ?= xcrun -sdk iphoneos tapi
TAPI_FLAGS  ?= stubify --no-uuids --filetype=tbd-v2

.PHONY: all clean

all: $(TARGET).dylib $(TARGET).tbd

$(TARGET).dylib: $(SRC)/*.c $(INC)/*.h
	$(IGCC) $(IGCC_FLAGS) $(DYLIB_FLAGS) -o $@ $(SRC)/*.c
	$(SIGN) $(SIGN_FLAGS) $@

$(TARGET).tbd: $(TARGET).dylib
	$(TAPI) $(TAPI_FLAGS) -o $@ $<

clean:
	rm -f $(TARGET).dylib $(TARGET).tbd
