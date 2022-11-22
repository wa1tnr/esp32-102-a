
# out of date:
#     pio project init --board esp32doit-devkit-v1

# latest:
#     pio project init --board adafruit_feather_esp32_v2

all:
	pio run

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

spiffs: erase
	@echo HERE IS SPIFFS
	@sleep 2
	pio run -t uploadfs

# no spiffs upload before terminal launch:
nsterm:
	sleep 2; ./cf-fastpico9t /dev/ttyACM0

term:
	sleep 2; ./cf-fastpico9t /dev/ttyACM0

upload:
	pio run -t upload

install: spiffs upload term

clean:
	pio run -t clean

distclean: clean
	rm -rf ./.pio
