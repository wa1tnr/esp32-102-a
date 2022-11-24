
#     pio project init --board adafruit_feather_esp32_v2

all:
	pio run

monitor:
	pio run -t monitor

# https://docs.platformio.org/en/latest/platforms/espressif32.html#erase-flash

erase:
	@echo HERE IS ERASE
	pio run -t erase
	@sleep 2

# https://community.platformio.org/t/i-cannot-figure-out-how-to-upload-a-spiffs-image/6172

devlist:
	pio device list

# Hardware ID: USB VID:PID=1A86:55D4 SER=54B0005774 LOCATION=2-1.4:1.0
# Description: USB Single Serial

# https://community.platformio.org/t/i-cannot-figure-out-how-to-upload-a-spiffs-image/6172

upload:
	pio run -t upload

install: upload

clean:
	pio run -t clean

distclean: clean
	rm -rf ./.pio
