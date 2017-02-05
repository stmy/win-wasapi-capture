#include <string>
#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

extern "C" {
#include "../../win-capture/obfuscate.h"
#include "../../win-capture/inject-library.h"
#define class class__avoid_cxx_keyword_conflict // evil
#include "../../win-capture/window-helpers.h"
#undef class
}

#include "win-wasapi-capture.h"
#include "wasapi-hook-info.h"

//-----------------------------------------------------------------------------
// Constants
//
//

namespace {
	const char* plugin_name = "WASAPICapture";
	const char* setting_target_window = "WASAPICapture.TargetWindow";
	const char* setting_target_window_desc = "WASAPICapture.TargetWindow.Description";
}

//-----------------------------------------------------------------------------
// Constructor and destructor of wasapi_capture class
//
//

wasapi_capture::wasapi_capture(obs_source_t* source)
{
	this->source = source;

	capture_thread = CreateThread(nullptr, 0,
		(LPTHREAD_START_ROUTINE)wasapi_capture::capture_thread_proc_proxy,
		this, 0, nullptr);
	keepalive_thread = CreateThread(nullptr, 0,
		(LPTHREAD_START_ROUTINE)wasapi_capture::keepalive_thread_proc_proxy,
		this, 0, nullptr);

	process_id = 0;
	destroying = false;
	target_process = INVALID_HANDLE_VALUE;
}

wasapi_capture::~wasapi_capture()
{
	destroying = true;
	WaitForSingleObject(keepalive_thread, INFINITE);
	WaitForSingleObject(capture_thread, INFINITE);
	eject();
	free_pipe();
	free_shared_memory();
	free_events();
}

//-----------------------------------------------------------------------------
// Member methods of wasapi_capture class
//
//

void wasapi_capture::keepalive_thread_proc_proxy(LPVOID param)
{
	((wasapi_capture*)param)->keepalive_thread_proc();
}

void wasapi_capture::keepalive_thread_proc()
{
	while (!destroying)
	{
		SetEvent(event_keepalive);
		Sleep(min(1000, KEEPALIVE_TIMEOUT / 2));

		update_capture();
	}
}

void wasapi_capture::capture_thread_proc_proxy(LPVOID param)
{
	((wasapi_capture*)param)->capture_thread_proc();
}

// taken from obs-studio/plugins/win-wasapi/win-wasapi.cpp
speaker_layout wasapi_capture::convert_speaker_layout(WAVEFORMATEXTENSIBLE* wfext)
{

#define KSAUDIO_SPEAKER_4POINT1 (KSAUDIO_SPEAKER_QUAD|SPEAKER_LOW_FREQUENCY)
#define KSAUDIO_SPEAKER_2POINT1 (KSAUDIO_SPEAKER_STEREO|SPEAKER_LOW_FREQUENCY)

	switch (wfext->dwChannelMask) {
	case KSAUDIO_SPEAKER_QUAD:             return SPEAKERS_QUAD;
	case KSAUDIO_SPEAKER_2POINT1:          return SPEAKERS_2POINT1;
	case KSAUDIO_SPEAKER_4POINT1:          return SPEAKERS_4POINT1;
	case KSAUDIO_SPEAKER_SURROUND:         return SPEAKERS_SURROUND;
	case KSAUDIO_SPEAKER_5POINT1:          return SPEAKERS_5POINT1;
	case KSAUDIO_SPEAKER_5POINT1_SURROUND: return SPEAKERS_5POINT1_SURROUND;
	case KSAUDIO_SPEAKER_7POINT1:          return SPEAKERS_7POINT1;
	case KSAUDIO_SPEAKER_7POINT1_SURROUND: return SPEAKERS_7POINT1_SURROUND;
	}

	return (speaker_layout)wfext->Format.nChannels;
}

audio_format wasapi_capture::convert_audio_format(WAVEFORMATEXTENSIBLE* wfext)
{
	const WORD format = wfext->Format.wFormatTag;
	const GUID subformat = wfext->SubFormat;
	const WORD bits = wfext->Format.wBitsPerSample;

	if (format == WAVE_FORMAT_PCM ||
		format == WAVE_FORMAT_EXTENSIBLE &&
		subformat == KSDATAFORMAT_SUBTYPE_PCM) 
	{
		switch (bits) {
		case 8: return AUDIO_FORMAT_U8BIT;
		case 16: return AUDIO_FORMAT_16BIT;
		case 32: return AUDIO_FORMAT_32BIT;
		}
	} else if (
		format == WAVE_FORMAT_IEEE_FLOAT ||
		format == WAVE_FORMAT_EXTENSIBLE &&
		subformat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) 
	{
		if (bits == 32) {
			return AUDIO_FORMAT_FLOAT;
		}
	}

	return AUDIO_FORMAT_UNKNOWN;
}

bool wasapi_capture::receive_audio_packet()
{
	DWORD bytes_read;
	audio_packet_header header;
	if (!ReadFile(pipe, &header, sizeof(header), &bytes_read, nullptr) ||
		bytes_read != sizeof(header) ||
		header.magic != AUDIO_PACKET_MAGIC)
	{
		return false; // failed reading header
	}

	// Extend buffer if required
	if (capture_buflen < header.data_length) {
		capture_buflen = header.data_length;
		capture_buffer = (uint8_t*)realloc(capture_buffer, capture_buflen);
		if (!capture_buffer) {
			destroying = true;
			return false; // out of memory
		}
	}

	if (!ReadFile(pipe, capture_buffer, header.data_length, &bytes_read, nullptr) ||
		bytes_read != header.data_length)
	{
		return false; // invalid data.
	}

	obs_source_audio audio;
	audio.format          = convert_audio_format(&header.wfext);
	audio.frames          = header.frames;
	audio.samples_per_sec = header.wfext.Format.nSamplesPerSec;
	audio.speakers        = convert_speaker_layout(&header.wfext);
	audio.timestamp       = header.timestamp;
	audio.data[0]         = capture_buffer;

	obs_source_output_audio(source, &audio);

	return true;
}

void wasapi_capture::capture_thread_proc()
{
	capture_buflen = 441 * 8 * 32; // 44.1kHz, 8ch, 32bit, 100ms
	capture_buffer = (uint8_t*)malloc(capture_buflen);

	while (!destroying) {
		DWORD wait_result = WaitForSingleObject(event_packet_sent, 500);
		if (wait_result != WAIT_OBJECT_0 || destroying) {
			continue;
		}

		while (os_atomic_load_long(&shared_data->packets) > 0) {
			bool receive_result = receive_audio_packet();
			os_atomic_dec_long(&shared_data->packets);

			if (!receive_result) {

				// TODO: resetting pipe is required here

				break;
			}
		}

		ResetEvent(event_packet_sent);
	}

	free(capture_buffer);
}

HANDLE wasapi_capture::open_process_obf(DWORD desired_access,
	bool inherit_handle, DWORD process_id)
{
	typedef HANDLE (WINAPI *open_process_t)(DWORD, BOOL, DWORD);
	static open_process_t fn_open_process;

	if (!fn_open_process)
	{
		static HMODULE kernel32_handle = NULL;
		if (!kernel32_handle)
			kernel32_handle = GetModuleHandleW(L"kernel32");

		fn_open_process = (open_process_t)get_obfuscated_func(
			kernel32_handle, "NuagUykjcxr", 0x1B694B59451ULL);
	}

	return fn_open_process(desired_access, inherit_handle, process_id);
}

bool wasapi_capture::is_wow64_process(HANDLE proc, BOOL* is_wow64)
{
	typedef HANDLE (WINAPI *is_wow64_process_t)(HANDLE, BOOL*);
	static is_wow64_process_t fn_is_wow64_process;

	if (!fn_is_wow64_process) {
		static HMODULE kernel32_handle = NULL;
		if (!kernel32_handle)
			kernel32_handle = GetModuleHandleW(L"kernel32");

		fn_is_wow64_process = (is_wow64_process_t)GetProcAddress(
			kernel32_handle, "IsWow64Process");

		if (!fn_is_wow64_process) return false;
	}

	fn_is_wow64_process(proc, is_wow64);

	return true;
}

bool wasapi_capture::is_64bit_target(HANDLE proc)
{
	BOOL is_wow64;
	if (!is_wow64_process(proc, &is_wow64)) {
		return false;
	}

	if (is_wow64) {
		return false; // 32-bit app on 64bit OS
	} else {
#ifdef _WIN64
		return true;
#else
		if (!is_wow64_process(GetCurrentProcess(), &is_wow64) || !is_wow64) {
			return false;
		} else {
			return true;
		}
#endif
	}
}

void wasapi_capture::inject_direct(bool is_64bit, HANDLE proc)
{
	char* dll_rel_name;
	if (is_64bit) {
		dll_rel_name = obs_module_file("wasapi-hook64.dll");
	} else {
		dll_rel_name = obs_module_file("wasapi-hook32.dll");
	}

	// char -> wchar_t
	wchar_t* wcs_dll_rel_name;
	os_utf8_to_wcs_ptr(dll_rel_name, 0, &wcs_dll_rel_name);

	// relative -> absolute
	wchar_t dll_abs_name[MAX_PATH * 2];
	_wfullpath(dll_abs_name, wcs_dll_rel_name, MAX_PATH * 2);

	int result = inject_library_obf(proc, dll_abs_name,
		"D|hkqkW`kl{k\\osofj", 0xa178ef3655e5ade7,
		"[uawaRzbhh{tIdkj~~", 0x561478dbd824387c,
		"[fr}pboIe`dlN}", 0x395bfbc9833590fd,
		"\\`zs}gmOzhhBq", 0x12897dd89168789a,
		"GbfkDaezbp~X", 0x76aff7238788f7db);

	bfree(dll_rel_name);
	bfree(wcs_dll_rel_name);
}

void wasapi_capture::inject_with_helper(bool is_64bit)
{
	wchar_t* dll_name;
	char* exe_rel_name;
	if (is_64bit) {
		dll_name = L"wasapi-hook64.dll";
		exe_rel_name = obs_module_file("inject-helper64.exe");
	} else {
		dll_name = L"wasapi-hook32.dll";
		exe_rel_name = obs_module_file("inject-helper32.exe");
	}

	wchar_t* wcs_exe_rel_name;
	os_utf8_to_wcs_ptr(exe_rel_name, 0, &wcs_exe_rel_name);

	wchar_t cmdline[4096];
	swprintf(cmdline, 4096, L"\"%s\" \"%s\" %lu %lu",
		wcs_exe_rel_name, dll_name, (unsigned long)false, process_id);

	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFO si = { 0 };
	BOOL r = CreateProcessW(wcs_exe_rel_name, cmdline, NULL, NULL,
		false, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

	bfree(exe_rel_name);
	bfree(wcs_exe_rel_name);
}

void wasapi_capture::inject()
{
	target_process = open_process_obf(PROCESS_ALL_ACCESS, false, process_id);
	bool is_64bit = is_64bit_target(target_process);

#ifdef _WIN64
	inject_direct(is_64bit, target_process);
#else
	if (is_64bit) inject_with_helper(is_64bit);
	else          inject_direct(is_64bit, target_process);
#endif
}

void wasapi_capture::eject()
{
	CloseHandle(target_process);
	target_process = INVALID_HANDLE_VALUE;
	SetEvent(event_exit);
	SetEvent(event_keepalive);
}

void wasapi_capture::init_events()
{
#define CREATE_EVENT(name) CreateEventA( \
		nullptr, FALSE, FALSE, \
		(std::string(name) + std::to_string(process_id)).c_str());

	event_capture_start   = CREATE_EVENT(EVENT_CAPTURE_START);
	event_capture_stop    = CREATE_EVENT(EVENT_CAPTURE_STOP);
	event_capture_restart = CREATE_EVENT(EVENT_CAPTURE_RESTART);
	event_ready           = CREATE_EVENT(EVENT_READY);
	event_exit            = CREATE_EVENT(EVENT_EXIT);
	event_packet_sent     = CREATE_EVENT(EVENT_PACKET_SENT);
	event_keepalive       = CREATE_EVENT(EVENT_KEEPALIVE);


#undef CREATE_EVENT
}

void wasapi_capture::free_events()
{
	CloseHandle(event_capture_start);
	CloseHandle(event_capture_stop);
	CloseHandle(event_capture_restart);
	CloseHandle(event_ready);
	CloseHandle(event_exit);
	CloseHandle(event_packet_sent);
	CloseHandle(event_keepalive);
}

void wasapi_capture::init_pipe()
{
	std::string pipe_name("\\\\.\\pipe\\" STREAM_PIPE_NAME);
	pipe_name += std::to_string(process_id);

	pipe = CreateNamedPipeA(pipe_name.c_str(),
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE,
		2,
		102400,
		102400,
		INFINITE,
		NULL);
}

void wasapi_capture::free_pipe()
{
	CloseHandle(pipe);
	pipe = NULL;
}

void wasapi_capture::init_shared_memory()
{
	std::string shared_memory_name(SHMEM_NAME);
	shared_memory_name += std::to_string(process_id);

	shmem = CreateFileMappingA(
		INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 
		sizeof(shmem_data), shared_memory_name.c_str());

	shared_data = (shmem_data*)MapViewOfFile(
		shmem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shmem_data));
}

void wasapi_capture::free_shared_memory()
{
	UnmapViewOfFile(shared_data);
	CloseHandle(shmem);
	shared_data = nullptr;
	shmem = NULL;
}

void wasapi_capture::exit_capture()
{
	eject();
	free_pipe();
	free_events();
	free_shared_memory();
	process_id = 0;
}

void wasapi_capture::start_capture()
{
	init_events();
	init_pipe();
	init_shared_memory();
	ResetEvent(event_exit);
	inject();
}

void wasapi_capture::update_capture()
{
	// Rehook if target is changed
	DWORD new_pid = get_target_process_id();
	DWORD prev_pid = process_id;

	if (prev_pid != new_pid && prev_pid != 0) {
		exit_capture();
	}

	if (new_pid != prev_pid && new_pid != 0) {
		process_id = new_pid;
		start_capture();
	}

}

void wasapi_capture::update_settings(obs_data_t* settings)
{
	this->settings = settings;

	update_capture();
}

DWORD wasapi_capture::get_target_process_id()
{
	char* title;
	char* class_name;
	char* file_name;
	auto window = obs_data_get_string(settings, setting_target_window);
	build_window_strings(window, &class_name, &title, &file_name);

	HWND hwnd = find_window(INCLUDE_MINIMIZED, WINDOW_PRIORITY_EXE, 
		class_name, title, file_name);

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);

	return pid;
}

//-----------------------------------------------------------------------------
// Functions for obs_source_info
//
//

const char* wasapi_capture::get_name(void* data)
{
	return plugin_name;
}

void* wasapi_capture::create(obs_data_t* settings, obs_source_t* source)
{
	wasapi_capture* wc = new wasapi_capture(source);
	wc->update_settings(settings);
	return wc;
}

void wasapi_capture::destroy(void* data)
{
	delete reinterpret_cast<wasapi_capture*>(data);
}

void wasapi_capture::get_defaults(obs_data_t* settings)
{

}

static const char *blacklisted_exes[] = {
	"explorer",
	"steam",
	"battle.net",
	"galaxyclient",
	"skype",
	"uplay",
	"origin",
	"devenv",
	"taskmgr",
	"systemsettings",
	"applicationframehost",
	"cmd",
	"shellexperiencehost",
	"winstore.app",
	"searchui",
	NULL
};

static bool is_blacklisted_exe(const char *exe)
{
	char cur_exe[MAX_PATH];

	if (!exe)
		return false;

	for (const char **vals = blacklisted_exes; *vals; vals++) {
		strcpy(cur_exe, *vals);
		strcat(cur_exe, ".exe");

		if (strcmpi(cur_exe, exe) == 0)
			return true;
	}

	return false;
}

static bool window_not_blacklisted(const char *title, const char *class_name,
	const char *exe)
{
	UNUSED_PARAMETER(title);
	UNUSED_PARAMETER(class_name);

	return !is_blacklisted_exe(exe);
}

static void insert_preserved_val(obs_property_t *p, const char *val)
{
	char *class_name = NULL;
	char *title = NULL;
	char *executable = NULL;
	struct dstr desc = { 0 };

	build_window_strings(val, &class_name, &title, &executable);

	dstr_printf(&desc, "[%s]: %s", executable, title);
	obs_property_list_insert_string(p, 1, desc.array, val);
	obs_property_list_item_disable(p, 1, true);

	dstr_free(&desc);
	bfree(class_name);
	bfree(title);
	bfree(executable);
}

static bool window_changed_callback(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	const char *cur_val;
	bool match = false;
	size_t i = 0;

	cur_val = obs_data_get_string(settings, setting_target_window);
	if (!cur_val) {
		return false;
	}

	for (;;) {
		const char *val = obs_property_list_item_string(p, i++);
		if (!val)
			break;

		if (strcmp(val, cur_val) == 0) {
			match = true;
			break;
		}
	}

	if (cur_val && *cur_val && !match) {
		insert_preserved_val(p, cur_val);
		return true;
	}

	UNUSED_PARAMETER(ppts);
	return false;
}

obs_properties_t* wasapi_capture::get_properties(void* data)
{
	obs_properties_t* props = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_list(props,
		setting_target_window, setting_target_window_desc, 
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "", "");
	fill_window_list(p, INCLUDE_MINIMIZED, window_not_blacklisted);
	obs_property_set_modified_callback(p, window_changed_callback);

	return props;
}

void wasapi_capture::update(void* data, obs_data_t* settings)
{
	reinterpret_cast<wasapi_capture*>(data)->update_settings(settings);
}



