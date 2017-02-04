#include <string>
#include <util/platform.h>
#include <util/threading.h>
#include "win-wasapi-capture.h"
extern "C" {
#include "../../win-capture/obfuscate.h"
#include "../../win-capture/inject-library.h"
#define class class__avoid_cxx_keyword_conflict // evil
#include "../../win-capture/window-helpers.h"
#undef class
}
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
}

wasapi_capture::~wasapi_capture()
{
	
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
		Sleep(KEEPALIVE_TIMEOUT / 2);
	}
}

void wasapi_capture::capture_thread_proc_proxy(LPVOID param)
{
	((wasapi_capture*)param)->capture_thread_proc();
}

void wasapi_capture::capture_thread_proc()
{
	DWORD result, bytes_read;
	audio_packet_header header;
	size_t current_buflen = 1024;
	uint8_t* buffer = (uint8_t*)malloc(current_buflen);

	while (!destroying)
	{
		result = WaitForSingleObject(event_packet_sent, INFINITE);
		ResetEvent(event_packet_sent);
		if (result == WAIT_OBJECT_0 && !destroying) {
			while (os_atomic_load_long(&shared_data->packets) > 0) {
				ReadFile(pipe, &header, sizeof(header), &bytes_read, nullptr);

				if (current_buflen < header.data_length) {
					buffer = (uint8_t*)realloc(buffer, header.data_length);
				}

				ReadFile(pipe, buffer, header.data_length, &bytes_read, nullptr);

				obs_source_audio audio;
				audio.format = AUDIO_FORMAT_16BIT;
				audio.frames = header.frames;
				audio.samples_per_sec = header.wfext.Format.nSamplesPerSec;
				audio.speakers = SPEAKERS_7POINT1_SURROUND;
				audio.timestamp = os_gettime_ns();
				audio.data[0] = buffer;

				obs_source_output_audio(source, &audio);

				os_atomic_dec_long(&shared_data->packets);
			}
		}
	}

	free(buffer);
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
		is_wow64_process(GetCurrentProcess(), &is_wow64);
		if (is_wow64) {
			return false;
		} else {
			return true;
		}
	}
}

void wasapi_capture::inject()
{
	HANDLE proc = open_process_obf(PROCESS_ALL_ACCESS, false, process_id);     
	bool is_64bit = is_64bit_target(proc);

	// Create absolute path to the DLL to inject
	const char* dll_rel_name;
	if (is_64bit) {
		dll_rel_name = obs_module_file("wasapi-hook64.dll");
	} else {
		dll_rel_name = obs_module_file("wasapi-hook32.dll");
	}

	wchar_t* wcs_dll_rel_name;
	os_utf8_to_wcs_ptr(dll_rel_name, 0, &wcs_dll_rel_name);

	wchar_t dll_abs_name[MAX_PATH*2];
	_wfullpath(dll_abs_name, wcs_dll_rel_name, MAX_PATH*2);
	bfree(wcs_dll_rel_name);

	// Inject!
	int result = inject_library_obf(proc, dll_abs_name,
		"D|hkqkW`kl{k\\osofj", 0xa178ef3655e5ade7,
		"[uawaRzbhh{tIdkj~~", 0x561478dbd824387c,
		"[fr}pboIe`dlN}", 0x395bfbc9833590fd,
		"\\`zs}gmOzhhBq", 0x12897dd89168789a,
		"GbfkDaezbp~X", 0x76aff7238788f7db);

	CloseHandle(proc);
}

void wasapi_capture::eject()
{
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
		INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(shmem_data),
		shared_memory_name.c_str());

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

void wasapi_capture::updated(obs_data_t* settings)
{
	this->settings = settings;

	// Rehook if target is changed
	DWORD new_pid = get_target_process_id();

	if (process_id != new_pid && process_id != 0) {
		eject();
		free_pipe();
		free_events();
		free_shared_memory();
	}
	process_id = new_pid;
	if (process_id != 0) {
		init_events();
		init_pipe();
		init_shared_memory();
		ResetEvent(event_exit);
		inject();
	}
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
	wc->updated(settings);
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
	reinterpret_cast<wasapi_capture*>(data)->updated(settings);
}



