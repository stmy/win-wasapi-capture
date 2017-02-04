#include <obs-module.h>
#include <string>
#include <windows.h>

enum window_priority;
struct shmem_data;

class wasapi_capture {
private:
	obs_source_t*    source;
	obs_data_t*      settings;

	HANDLE           injector_process;
	DWORD            process_id;
	bool             is_64bit;
	DWORD            thread_id;
	HANDLE           pipe;
	HANDLE           shmem;
	shmem_data*      shared_data;
	HANDLE           capture_thread;
	HANDLE           keepalive_thread;
	bool             destroying;

	std::string      title;
	std::string      class_name;
	std::string      executable;
	window_priority  priority;

	HANDLE           event_capture_start;
	HANDLE           event_capture_stop;
	HANDLE           event_capture_restart;
	HANDLE           event_ready;
	HANDLE           event_exit;
	HANDLE           event_packet_sent;
	HANDLE           event_keepalive;

public: /* obs_source_info */
	static const char* get_name(void* data);
	static void* create(obs_data_t* settings, obs_source_t* source);
	static void destroy(void* data);
	static void get_defaults(obs_data_t* settings);
	static obs_properties_t* get_properties(void* data);
	static void update(void* data, obs_data_t* settings);

public:
	wasapi_capture(obs_source_t* source);
	~wasapi_capture();

	
private:
	static void keepalive_thread_proc_proxy(LPVOID param);
	void keepalive_thread_proc();
	static void wasapi_capture::capture_thread_proc_proxy(LPVOID param);
	void capture_thread_proc();
	void init_events();
	void free_events();
	void inject();
	void eject();
	void init_pipe();
	void free_pipe();
	void init_shared_memory();
	void free_shared_memory();
	void updated(obs_data_t* settings);
	DWORD get_target_process_id();
	static HANDLE open_process_obf(DWORD desired_access,
			bool inherit_handle, DWORD process_id);
	static bool is_wow64_process(HANDLE proc, BOOL* is_wow64);
	static bool is_64bit_target(HANDLE proc);
};