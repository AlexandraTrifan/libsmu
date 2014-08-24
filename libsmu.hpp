#pragma once
#include "libsmu.h"
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cmath>

class Device;
class Signal;
struct libusb_device;
struct libusb_device_handle;
struct libusb_context;

class Session {
public:
	Session();
	~Session();

	int update_available_devices();
	std::vector<std::shared_ptr<Device>> m_available_devices;

	Device* add_device(Device*);
	std::set<Device*> m_devices;
	void remove_device(Device*);

	void configure(uint64_t sampleRate);
	std::function<void(sample_t)> m_progress_cb;

	// Run the currently configured capture and wait for it to complete
	void run(sample_t nsamples);
	void run_nonblocking(sample_t nsamples);
	void cancel();

	void completion();
protected:
	void start_usb_thread();
	std::thread m_usb_thread;
	bool m_usb_thread_loop;

	std::mutex m_lock;
	std::condition_variable m_completion;

	unsigned m_active_devices;

	libusb_context* m_usb_cx;
	std::shared_ptr<Device> probe_device(libusb_device* device);
	std::shared_ptr<Device> find_existing_device(libusb_device* device);
};

class Device {
public:
	virtual ~Device();
	virtual const sl_device_info* const info() = 0;
	virtual const sl_channel_info* const channel_info(unsigned channel) = 0;
	virtual Signal* signal(unsigned channel, unsigned signal) = 0;
	virtual const char* const serial() { return ""; }
	virtual void set_mode(unsigned channel, unsigned mode) {}

protected:
	Device(Session* s, libusb_device* d);
	virtual int init();
	virtual int added() {return 0;}
	virtual int removed() {return 0;}
	virtual void configure(uint64_t sampleRate) = 0;

	virtual void on() = 0;
	virtual void off() = 0;
	virtual void start_run(sample_t nsamples) = 0;
	virtual void cancel() = 0;

	Session* const m_session;
	libusb_device* const m_device;
	libusb_device_handle* m_usb;

	friend class Session;
};

enum Dest {
	DEST_NONE,
	DEST_BUFFER,
	DEST_CALLBACK,
};

enum Src {
	SRC_CONSTANT,
	SRC_SQUARE,
	SRC_SAWTOOTH,
	SRC_SINE,
	SRC_TRIANGLE,
	SRC_BUFFER,
	SRC_CALLBACK,
};

class Signal {
public:
	Signal(const sl_signal_info* info): m_info(info), m_src(SRC_CONSTANT), m_src_v1(0), m_dest(DEST_NONE) {}
	const sl_signal_info* const info() { return m_info; }
	const sl_signal_info* const m_info;

	void source_constant(value_t val) {
		m_src = SRC_CONSTANT;
		m_src_v1 = val;
	}
	void source_square(value_t v1, value_t v2, sample_t period, sample_t duty, int phase) {
		m_src = SRC_SQUARE;
		update_phase(period, phase);
		m_src_v1 = v1;
		m_src_v2 = v2;
		m_src_duty = duty;
	}
	void source_sawtooth(value_t v1, value_t v2, sample_t period, int phase) {
		m_src = SRC_SAWTOOTH;
		update_phase(period, phase);
		m_src_v1 = v1;
		m_src_v2 = v2;
	}
	void source_sine(value_t center, value_t amplitude, double period, int phase) {
		m_src = SRC_SINE;
		update_phase(period, phase);
		m_src_v1 = center;
		m_src_v2 = amplitude;
	}
	void source_triangle(value_t v1, value_t v2, double period, int phase) {
		m_src = SRC_TRIANGLE;
		update_phase(period, phase);
		m_src_v1 = v1;
		m_src_v2 = v2;
	}
	//void source_arb(arb_point_t* points, size_t len, bool repeat);
	void source_buffer(value_t* buf, size_t len, bool repeat) {
		m_src = SRC_BUFFER;
		m_src_buf = buf;
		m_src_buf_len = len;
		m_src_buf_repeat = repeat;
		m_src_i = 0;
	}
	void source_callback(std::function<value_t (sample_t index)> callback) {
		m_src = SRC_CALLBACK;
		m_src_callback = callback;
		m_src_i = 0;
	}

	value_t measure_instantaneous();
	void measure_buffer(value_t* buf, size_t len) {
		m_dest = DEST_BUFFER;
		m_dest_buf = buf;
		m_dest_buf_len = len;
	}
	void measure_callback(std::function<void(value_t value)>);

	inline void put_sample(value_t val) {
		m_latest_measurement = val;
		if (m_dest == DEST_BUFFER) {
			if (m_dest_buf_len) {
				*m_dest_buf++ = val;
				m_dest_buf_len -= 1;
			}
		} else if (m_dest == DEST_CALLBACK) {
			m_dest_callback(val);
		}
	}

	inline value_t get_sample() {
		switch (m_src) {
		case SRC_CONSTANT:
			return m_src_v1;

		case SRC_BUFFER:
			if (m_src_i >= m_src_buf_len) {
				if (m_src_buf_repeat) {
					m_src_i = 0;
				} else {
					return m_src_buf[m_src_buf_len-1];
				}
			}
			return m_src_buf[m_src_i++];


		case SRC_CALLBACK:
			return m_src_callback(m_src_i++);

		case SRC_SQUARE:
		case SRC_SAWTOOTH:
		case SRC_SINE:
		case SRC_TRIANGLE:

			auto phase = m_src_phase;
			auto norm_phase = phase / m_src_period;
			m_src_phase = fmod(m_src_phase + 1, m_src_period);

			switch (m_src) {
			case SRC_SQUARE:
				return (phase < m_src_duty) ? m_src_v1 : m_src_v2;

			case SRC_SAWTOOTH:
				return m_src_v1 + norm_phase * (m_src_v2 - m_src_v1);

			case SRC_SINE:
				return m_src_v1 + cos(norm_phase * 2 * M_PI) * m_src_v2;

			case SRC_TRIANGLE:
				return m_src_v1 + fabs(norm_phase*2 - 1) * (m_src_v2 - m_src_v1);

			default:
				return 0;
			}
		}
	}
protected:
	Src m_src;
	value_t m_src_v1;
	value_t m_src_v2;
	double m_src_period;
	double m_src_duty;
	double m_src_phase;

	value_t* m_src_buf;
	size_t m_src_i;
	size_t m_src_buf_len;
	bool m_src_buf_repeat;

	void update_phase(double new_period, double new_phase) {
		m_src_phase = new_phase;
		m_src_period = new_period;
	}

	std::function<value_t (sample_t index)> m_src_callback;

	Dest m_dest;

	// valid if m_dest == DEST_BUF
	value_t* m_dest_buf;
	size_t m_dest_buf_len;

	// valid if m_dest == DEST_CALLBACK
	std::function<void(value_t val)> m_dest_callback;

	value_t m_latest_measurement;
};
