
# out of date:
#     pio project init --board esp32doit-devkit-v1

all:
	pio run

upload:
	pio run -t upload

install: upload
	sleep 4; ./cf-fastpico9t /dev/ttyACM0

clean:
	pio run -t clean

distclean: clean
	rm -rf ./.pio
