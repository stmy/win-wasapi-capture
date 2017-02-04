#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <Windows.h>
#include <mmreg.h>

#include "../../win-capture/hook-helpers.h"

#define KEEPALIVE_TIMEOUT 5000

#define EVENT_CAPTURE_START   "WASAPICaptureHook_Start"
#define EVENT_CAPTURE_RESTART "WASAPICaptureHook_Restart"
#define EVENT_CAPTURE_STOP    "WASAPICaptureHook_Stop"
#define EVENT_READY           "WASAPICaptureHook_HookReady"
#define EVENT_EXIT            "WASAPICaptureHook_Exit"
#define EVENT_PACKET_SENT     "WASAPICaptureHook_PacketSent"
#define EVENT_KEEPALIVE       "WASAPICaptureHook_KeepAlive"

#define SHMEM_HOOK_INFO       "WASAPICaptureHook_HookInfo"
#define STREAM_PIPE_NAME      "WASAPICaptureHook_StreamPipe"
#define SHMEM_NAME            "WASAPICaptureHook_SharedMemory"

#pragma pack(push, 8)

struct shmem_data {
	volatile long          packets;
};

#define AUDIO_PACKET_MAGIC 0x6B635041

struct audio_packet_header {
	uint32_t               magic;
	uint32_t               frames;
	WAVEFORMATEXTENSIBLE   wfext;
	uint64_t               timestamp;
	uint32_t               data_length;
};

struct audio_packet {
	audio_packet_header    header;
	uint8_t                data[1];
};

#pragma pack(pop)

