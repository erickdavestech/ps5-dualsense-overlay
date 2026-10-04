PS5_HOST ?=
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

CFLAGS := -Wall -Werror -O2

all: pad_probe.elf pad_rpc.elf

pad_probe.elf: main.c
	$(CC) $(CFLAGS) -o $@ $^ -lScePad -lSceUserService -lSceNotification

pad_rpc.elf: pad_rpc.c pt/pt.c
	$(CC) $(CFLAGS) -o $@ $^ -lSceNotification

clean:
	rm -f *.elf

test-rpc: pad_rpc.elf
	@test -n "$(PS5_HOST)" || { echo "usage: make test-rpc PS5_HOST=<PS5-IP>"; exit 1; }
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) pad_rpc.elf
