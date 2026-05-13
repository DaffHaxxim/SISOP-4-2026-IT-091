CC = gcc
CFLAGS = -Wall `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs`
TARGET = kenz_rescue
SRC = kenz_rescue.c
ZIP = amba_files.zip

all: setup $(TARGET)

setup:
	@if [ ! -d amba_files ]; then \
		if [ -f $(ZIP) ]; then \
			echo "Extracting $(ZIP)..."; \
			unzip -o $(ZIP); \
			mv amba_files/amba_files/* amba_files/ 2>/dev/null || true; \
			rmdir amba_files/amba_files 2>/dev/null || true; \
			rm -f $(ZIP); \
			rm -f amba_files/*.txt:Zone.Identifier; \
		else \
			echo "Error: $(ZIP) not found and amba_files/ missing"; \
			exit 1; \
		fi \
	fi
	@mkdir -p mnt

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -rf mnt

distclean: clean
	rm -rf amba_files

run: $(TARGET)
	mkdir -p mnt
	./$(TARGET) -f mnt

.PHONY: all setup clean run
