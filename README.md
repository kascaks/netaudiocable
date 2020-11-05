# Net Audio Cable

This is an attempt to stream audio from a notebook with windows 10 over wifi to a raspberry pi connected to speakers
fast enough so that watching movies on the notebook while having audio output from external speakers is posible
without a cable from the notebook to speakers.

This should be no problem given wifi throughputs and latencies achievable with today's widely used wifi routers,
since uncompressed CD quality audio requires about 150kB of throughput and video is interpreted in sync with audio
if the latency between the two is within 50ms.

In order to be able to stream all audio (e.g. audio of a youtube video running in browser), an audio driver
for a virtual audio card is needed that can be selected as audio output via windows settings. The driver sends uncompressed
PCM stream via UDP over the network to a raspberry pi on which incoming packets are directly played over ALSA.

Current implementation is in a proof-of-concept stage. The possibility to stream audio in realtime over wifi is verified.
Main limitation of the implementation is currently IP address (and port) of server
hardcoded in client and necessity to run windows in debug mode so that the driver unsigned by microsoft can be installed.

### Compilation and installation

Windows driver exists as a Visual Studio project which is used for compilation. Compiled driver is installed to windows
(which needs to run in debug mode) via `Device Manager -> Action -> Add legacy hardware`.

Linux part is just a single file which can be compiled as `gcc -o nac nac.c -lasound` and then executed as `./nac &`.
