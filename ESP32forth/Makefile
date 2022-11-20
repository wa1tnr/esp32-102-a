
# out of date:
#     pio project init --board esp32doit-devkit-v1

all:
	pio run

# https://community.platformio.org/t/i-cannot-figure-out-how-to-upload-a-spiffs-image/6172

spiffs:
	pio run -t uploadfs

# no spiffs upload before terminal launch:
nsterm:
	sleep 4; ./cf-fastpico9t /dev/ttyACM0

term: spiffs
	sleep 4; ./cf-fastpico9t /dev/ttyACM0

upload:
	pio run -t upload

install: upload term

clean:
	pio run -t clean

distclean: clean
	rm -rf ./.pio
