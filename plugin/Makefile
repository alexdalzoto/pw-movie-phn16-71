all: dts_agc.so dts_bass.so dts_vol.so
dts_agc.so: dts_agc.c
	$(CC) -O2 -fPIC -shared -Wall -o $@ $< -lm
dts_bass.so: dts_bass.c
	$(CC) -O2 -fPIC -shared -Wall -o $@ $< -lm
dts_vol.so: dts_vol.c
	$(CC) -O2 -fPIC -shared -Wall -o $@ $< -lm
dts_bass_test: dts_bass.c
	$(CC) -O2 -Wall -DBASS_TEST -o $@ $< -lm
install: dts_agc.so dts_bass.so dts_vol.so
	mkdir -p ~/.ladspa && cp dts_agc.so dts_bass.so dts_vol.so ~/.ladspa/
