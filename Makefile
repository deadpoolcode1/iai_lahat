# MPC555 UART bridge firmware build.
#
# Uses the Ubuntu `gcc-powerpc-linux-gnu` cross-toolchain in freestanding /
# bare-metal mode (`-nostdlib`, custom linker script, no startfiles).
# Output: build/lahat.elf, build/lahat.bin, build/lahat.srec.

CROSS    ?= powerpc-linux-gnu-
CC       := $(CROSS)gcc
LD       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size

BUILD    := build
TARGET   := $(BUILD)/lahat

SRC_C    := src/main.c src/sci.c src/timebase.c
SRC_S    := src/start.S
OBJS     := $(SRC_C:src/%.c=$(BUILD)/%.o) $(SRC_S:src/%.S=$(BUILD)/%.o)

LDSCRIPT := ld/mpc555.ld

# CPU / ABI flags. The MPC555 is a 32-bit big-endian classic PowerPC core.
# `-mcpu=powerpc` keeps us inside the base ISA the RCPU implements.
# `-msoft-float` because main() never touches the FPU and we don't enable
# it at startup. `-mno-sdata` keeps the .sdata/.sdata2 sections empty so
# the small-data anchors in start.S have nothing pointing at them yet still
# resolve cleanly.
CPUFLAGS := -mcpu=powerpc -mbig-endian -msoft-float -mno-sdata

CFLAGS   := $(CPUFLAGS) \
            -Os -g3 -std=c11 \
            -ffreestanding -fno-builtin -fno-common \
            -ffunction-sections -fdata-sections \
            -Wall -Wextra -Wpedantic -Wshadow -Wundef -Wstrict-prototypes \
            -Isrc

ASFLAGS  := $(CPUFLAGS) -g3

LDFLAGS  := $(CPUFLAGS) \
            -nostdlib -nostartfiles -Wl,--build-id=none \
            -Wl,--gc-sections \
            -Wl,-Map=$(TARGET).map \
            -T $(LDSCRIPT)

LDLIBS   := -lgcc

.PHONY: all clean size disasm
all: $(TARGET).elf $(TARGET).bin $(TARGET).srec size

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(LD) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).srec: $(TARGET).elf
	$(OBJCOPY) -O srec --srec-forceS3 --srec-len=32 $< $@

size: $(TARGET).elf
	@$(SIZE) $<

disasm: $(TARGET).elf
	$(CROSS)objdump -d $< | less

clean:
	rm -rf $(BUILD)
