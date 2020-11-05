#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include <byteswap.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>

unsigned int sample_rate = 44100;
snd_pcm_uframes_t buffer_size = 4096;

char loglevel = 2;

enum _level{
	error,
	warning,
	info,
	debug
};
typedef enum _level level;

enum _control_flag{
	flag_stop,
	flag_acquire,
	flag_pause,
	flag_run,
	flag_data_start,
	flag_data_stop
};
typedef enum _control_flag control_flag;

void slog(level lvl, const char* format, ...){
	if(lvl > loglevel){
		return;
	}
	va_list args;
	struct timespec t;
	struct tm* tm;
	static const char lvl_name[][8] = {"ERROR", "WARNING", "INFO", "DEBUG"};
	va_start(args, format);
	clock_gettime(CLOCK_REALTIME, &t);
	tm = localtime(&t.tv_sec);
	printf("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6ld %s ", 1900 + tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, t.tv_nsec / 1000, lvl_name[lvl]);
	vprintf(format, args);
	printf("\n");
	va_end(args);
}

int set_hwparams(snd_pcm_t *handle){
	int ret;
	snd_pcm_hw_params_t *params = NULL;
	snd_pcm_uframes_t buffer_size_min;
	snd_pcm_uframes_t buffer_size_max;

	snd_pcm_hw_params_alloca(&params);

	/* choose all parameters */
	ret = snd_pcm_hw_params_any(handle, params);
	if (ret < 0) {
		slog(error, "Broken configuration for playback: no configurations available: %s",
			snd_strerror(ret));
		return ret;
	}

	/* set the interleaved read/write format */
	ret = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		slog(error, "Access type not available for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	/* set the sample format */
	ret = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
	if (ret < 0) {
		slog(error, "Sample format not available for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	/* set the count of channels */
	ret = snd_pcm_hw_params_set_channels(handle, params, 2);
	if (ret < 0) {
		slog(error, "Channels count not available for playbacks: %s",
			snd_strerror(ret));
		return ret;
	}

	/* set the stream rate */
	ret = snd_pcm_hw_params_set_rate(handle, params, sample_rate, 0);
	if (ret < 0) {
		slog(error, "Rate not available for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	/* set the buffer size */
	if ((ret = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size))
			< 0) {
		slog(error, "Unable to set buffer size: %s", snd_strerror(ret));
		return ret;
	}

	/* write the parameters to device */
	ret = snd_pcm_hw_params(handle, params);
	if (ret < 0) {
		slog(error, "Unable to set hw params for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	return 0;
}

int set_swparams(snd_pcm_t *handle) {
	int ret;
	snd_pcm_sw_params_t *params;

	snd_pcm_sw_params_alloca(&params);

	/* get the current swparams */
	ret = snd_pcm_sw_params_current(handle, params);
	if (ret < 0) {
		slog(error, "Unable to determine current swparams for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_sw_params_set_start_threshold(handle, params, 768);
	if (ret < 0) {
		slog(error, "Unable to set start threshold mode for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	/* write the parameters to the playback device */
	ret = snd_pcm_sw_params(handle, params);
	if (ret < 0) {
		slog(error, "Unable to set sw params for playback: %s",
			snd_strerror(ret));
		return ret;
	}

	return 0;
}

/*
 * Undretun and suspend recovery
 */
int xrun_recovery(snd_pcm_t *handle, int ret) {
	if (ret == -EPIPE) { /* under-run */
		ret = snd_pcm_prepare(handle);
		if (ret < 0)
			slog(error, "Can't recover from underrun, prepare failed: %s",
				snd_strerror(ret));
	} else if (ret == -ESTRPIPE) { /* suspended drivers */
		while ((ret = snd_pcm_resume(handle)) == -EAGAIN)
			sleep(1); /* wait until the suspend flag is released */
		if (ret < 0) {
			ret = snd_pcm_prepare(handle);
			if (ret < 0)
				slog(error, "Can't recover from suspend, prepare failed: %s",
					snd_strerror(ret));
		}
	}
	return ret;
}

snd_pcm_t* pcm_init(){
	int ret;
	snd_pcm_t* handle = NULL;

	if ((ret = snd_pcm_open(&handle, "sysdefault", SND_PCM_STREAM_PLAYBACK, 0))
                        < 0) {
                slog(error, "Playback open error: %d,%s", ret, snd_strerror(ret));
                goto cleanup;
        }

        if ((ret = set_hwparams(handle))
                        < 0) {
                slog(error, "Setting of hwparams failed: %s", snd_strerror(ret));
                goto cleanup;
        }
        if ((ret = set_swparams(handle)) < 0) {
                slog(error, "Setting of swparams failed: %s", snd_strerror(ret));
                goto cleanup;
        }

cleanup:
	if(handle == NULL){
		snd_pcm_close(handle);
	}

	return handle;
}

int main(int argc, char *argv[]) {
	snd_pcm_t *handle = NULL;
	struct sockaddr_in bindaddr;
	struct sockaddr_in fromaddr;
	socklen_t fromaddrlen = sizeof(fromaddr);
	int sock;
	char* recvbuf;
	ssize_t recvlen;
	int datacountcheck;
	int ret;
	int recvbuflen = 1024;
	int32_t flag;
	char state = flag_stop;
	snd_pcm_sframes_t avail;
	snd_pcm_sframes_t delay;
	snd_pcm_state_t pcm_state;

	slog(info, "Starting");

	memset(&bindaddr, 0, sizeof(bindaddr));
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(8989);
	bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (bind(sock, (struct sockaddr*) &bindaddr, sizeof(bindaddr)) < 0) {
		slog(error, "Unable to bind socket %d: %s", errno, strerror(errno));
		goto cleanup;
	}

	recvbuf = malloc(recvbuflen);
	if(recvbuf == NULL){
		slog(error, "Unable to allocate memory for receive buffer");
		goto cleanup;
	}

	slog(info, "Started");

	while (1) {
		if ((recvlen = recvfrom(sock, recvbuf, recvbuflen, 0,
				(struct sockaddr *) &fromaddr, &fromaddrlen)) < 0) {
			slog(warning, "Unable to receive packet %d: %s", errno,
					strerror(errno));
		} else {
			slog(debug, "received: %zd", recvlen);
			if(handle != NULL){
				pcm_state = snd_pcm_state(handle);
				slog(debug, "pcm state: %hhd", pcm_state);
				if(pcm_state == SND_PCM_STATE_PREPARED || pcm_state == SND_PCM_STATE_RUNNING){
					ret = snd_pcm_avail_delay(handle, &avail, &delay);
					if(ret < 0){
						slog(warning, "Failed to retrieve avail/delay %d: %s", ret, snd_strerror(ret));
					}else{
						slog(debug, "avail/delay: %ld/%ld", avail, delay);
					}
				}else{
					slog(debug, "pcm not running");
				}
			}else{
				slog(debug, "pcm not initialized");
			}

			if (recvlen >= 4) {
				if(recvlen < 20 && recvbuf[0] == 0x00 && recvbuf[1] == 0xff && recvbuf[2] == 0x00 && recvbuf[3] == 0xff){
					// control packet
					int32_t flag = ((int32_t *) recvbuf)[1];
					switch(flag){
					case flag_stop:
						slog(info, "stop flag");
						ret = snd_pcm_close(handle);
						handle = NULL;
						if(ret < 0){
							slog(error, "Failed to close pcm %d: %s", ret, snd_strerror(ret));
							goto cleanup;
						}
						state = flag_stop;
						break;
					case flag_acquire:
						slog(info, "acquire flag");
						if(state == flag_stop){
							handle = pcm_init();
							if(handle == NULL){
								slog(error, "Failed to init pcm");
								goto cleanup;
							}
						}
						state = flag_acquire;
						break;
					case flag_pause:
						slog(info, "pause flag");
						if(state == flag_run && snd_pcm_state(handle) == SND_PCM_STATE_RUNNING){
							ret = snd_pcm_drain(handle);
							if(ret < 0){
								slog(error, "Failed to drain pcm %d: %s", ret, snd_strerror(ret));
							}
						}
						state = flag_pause;
						break;
					case flag_run:
						slog(info, "start flag");
						if(state == flag_pause && snd_pcm_state(handle) == SND_PCM_STATE_SETUP){
							ret = snd_pcm_prepare(handle);
							if(ret < 0){
								slog(error, "Failed to prepare pcm %d: %s", ret, snd_strerror(ret));
							}
						}
						// will start automatically if there is enough samples in buffer
						break;
					case flag_data_start:
						datacountcheck = ((int32_t*) recvbuf)[2];
						slog(debug, "data start flag. Bytes to receive %d", datacountcheck);
						break;
					case flag_data_stop:
						slog(debug, "data stop flag. Byte discrepancy %d", datacountcheck);
						break;
					default:
						slog(warning, "unknown flag %d", flag);
					}
					continue;
				}
				/* Audio data */
				if(handle == NULL){
					/* Happens if started during active playback (state change packets missed) */
					slog(info, "On-the-fly pcm init");
					handle = pcm_init();
					if(handle == NULL){
						slog(error, "Failed to init pcm");
						goto cleanup;
					}
				}
				int frames_to_send = recvlen / 4;
				datacountcheck -= recvlen;
				int offset = 0;
				while (frames_to_send > 0) {
					ret = snd_pcm_writei(handle, recvbuf + offset, frames_to_send);
					if (ret < 0) {
						slog(warning, "Write frames to device failed %d: %s", ret, snd_strerror(ret));
						if (xrun_recovery(handle, ret) < 0) {
							slog(error, "xrun_recovery failed %d: %s",
									ret, snd_strerror(ret));
							goto cleanup;
						}
					} else {
						offset += ret * 4;
						frames_to_send -= ret;
					}
				}
				if(state != flag_run && snd_pcm_state(handle) == SND_PCM_STATE_RUNNING){
					state = flag_run;
				}
			}

		}
	}

	slog(info, "Finishing");

cleanup:
	if(recvbuf != NULL){
		free(recvbuf);
	}
	if(handle != NULL){
		snd_pcm_close(handle);
		handle = NULL;
	}

	snd_config_update_free_global();

	slog(info, "Finished");

	return ret;
}


