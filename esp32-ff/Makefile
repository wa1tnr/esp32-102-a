
# pio project init --board esp32doit-devkit-v1

all:
	pio run

erase:
	@echo HERE IS ERASE 28 Feb
	pio run -t erase

install:
	pio run -t upload

clean:
	pio run -t clean

distclean: clean
	rm -rf ./.pio
