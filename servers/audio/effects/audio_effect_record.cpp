/*************************************************************************/
/*  audio_effect_record.cpp                                              */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "audio_effect_record.h"

void AudioEffectRecordInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	if (!is_recording) {
		return;
	}

	//Add incoming audio frames to the IO ring buffer
	const AudioFrame *src = p_src_frames;
	AudioFrame *rb_buf = ring_buffer.ptrw();
	for (int i = 0; i < p_frame_count; i++) {
		rb_buf[ring_buffer_pos & ring_buffer_mask] = src[i];
		ring_buffer_pos++;
	}
}

bool AudioEffectRecordInstance::process_silence() {
	return true;
}

void AudioEffectRecordInstance::_io_thread_process() {

	//Reset recorder status
	thread_active = true;
	ring_buffer_pos = 0;
	ring_buffer_read_pos = 0;

	//We start a new recording
	recording_data.resize(0); //Clear data completely and reset length
	is_recording = true;

	while (is_recording) {
		//Check: The current recording has been requested to stop
		if (is_recording && !base->should_record) {
			is_recording = false;
		}

		//Case: Frames are remaining in the buffer
		if (ring_buffer_read_pos < ring_buffer_pos) {
			//Read from the buffer into recording_data
			_io_store_buffer();
		}
		//Case: The buffer is empty
		else if (is_recording) {
			//Wait to avoid too much busy-wait
			OS::get_singleton()->delay_usec(500);
		}
	}

	thread_active = false;
}

void AudioEffectRecordInstance::_io_store_buffer() {
	int to_read = ring_buffer_pos - ring_buffer_read_pos;

	AudioFrame *rb_buf = ring_buffer.ptrw();

	while (to_read) {
		AudioFrame buffered_frame = rb_buf[ring_buffer_read_pos & ring_buffer_mask];
		recording_data.push_back(buffered_frame.l);
		recording_data.push_back(buffered_frame.r);

		ring_buffer_read_pos++;
		to_read--;
	}
}

void AudioEffectRecordInstance::_thread_callback(void *_instance) {

	AudioEffectRecordInstance *aeri = reinterpret_cast<AudioEffectRecordInstance *>(_instance);

	aeri->_io_thread_process();
}

void AudioEffectRecordInstance::init() {
	io_thread = Thread::create(_thread_callback, this);
}

Ref<AudioEffectInstance> AudioEffectRecord::instance() {
	Ref<AudioEffectRecordInstance> ins;
	ins.instance();
	ins->base = Ref<AudioEffectRecord>(this);
	ins->is_recording = false;

	//Re-using the buffer size calculations from audio_effect_delay.cpp
	float ring_buffer_max_size = IO_BUFFER_SIZE_MS;
	ring_buffer_max_size /= 1000.0; //convert to seconds
	ring_buffer_max_size *= AudioServer::get_singleton()->get_mix_rate();

	int ringbuff_size = ring_buffer_max_size;

	int bits = 0;

	while (ringbuff_size > 0) {
		bits++;
		ringbuff_size /= 2;
	}

	ringbuff_size = 1 << bits;
	ins->ring_buffer_mask = ringbuff_size - 1;
	ins->ring_buffer_pos = 0;

	ins->ring_buffer.resize(ringbuff_size);

	ins->ring_buffer_read_pos = 0;

	ensure_thread_stopped();
	current_instance = ins;
	if (should_record) {
		ins->init();
	}

	return ins;
}

void AudioEffectRecord::ensure_thread_stopped() {
	should_record = false;
	if (current_instance != 0 && current_instance->thread_active) {
		Thread::wait_to_finish(current_instance->io_thread);
	}
}

void AudioEffectRecord::set_should_record(bool p_record) {
	if (p_record) {
		ensure_thread_stopped();
		current_instance->init();
	}

	should_record = p_record;
}

bool AudioEffectRecord::get_should_record() const {
	return should_record;
}

void AudioEffectRecord::set_format(AudioStreamSample::Format p_format) {
	format = p_format;
}

AudioStreamSample::Format AudioEffectRecord::get_format() const {
	return format;
}

Ref<AudioStreamSample> AudioEffectRecord::get_recording() const {
	AudioStreamSample::Format dst_format = format;
	bool stereo = true; //forcing mono is not implemented

	PoolVector<uint8_t> dst_data;

	if (dst_format == AudioStreamSample::FORMAT_8_BITS) {
		int data_size = current_instance->recording_data.size();
		dst_data.resize(data_size);
		PoolVector<uint8_t>::Write w = dst_data.write();

		for (int i = 0; i < data_size; i++) {
			int8_t v = CLAMP(current_instance->recording_data[i] * 128, -128, 127);
			w[i] = v;
		}
	} else if (dst_format == AudioStreamSample::FORMAT_16_BITS) {
		int data_size = current_instance->recording_data.size();
		dst_data.resize(data_size * 2);
		PoolVector<uint8_t>::Write w = dst_data.write();

		for (int i = 0; i < data_size; i++) {
			int16_t v = CLAMP(current_instance->recording_data[i] * 32768, -32768, 32767);
			encode_uint16(v, &w[i * 2]);
		}
	} else if (dst_format == AudioStreamSample::FORMAT_IMA_ADPCM) {
		//byte interleave
		Vector<float> left;
		Vector<float> right;

		int tframes = current_instance->recording_data.size() / 2;
		left.resize(tframes);
		right.resize(tframes);

		for (int i = 0; i < tframes; i++) {
			left.set(i, current_instance->recording_data[i * 2 + 0]);
			right.set(i, current_instance->recording_data[i * 2 + 1]);
		}

		PoolVector<uint8_t> bleft;
		PoolVector<uint8_t> bright;

		ResourceImporterWAV::_compress_ima_adpcm(left, bleft);
		ResourceImporterWAV::_compress_ima_adpcm(right, bright);

		int dl = bleft.size();
		dst_data.resize(dl * 2);

		PoolVector<uint8_t>::Write w = dst_data.write();
		PoolVector<uint8_t>::Read rl = bleft.read();
		PoolVector<uint8_t>::Read rr = bright.read();

		for (int i = 0; i < dl; i++) {
			w[i * 2 + 0] = rl[i];
			w[i * 2 + 1] = rr[i];
		}
	} else {
		ERR_EXPLAIN("format not implemented");
	}

	Ref<AudioStreamSample> sample;
	sample.instance();
	sample->set_data(dst_data);
	sample->set_format(dst_format);
	sample->set_mix_rate(AudioServer::get_singleton()->get_mix_rate());
	sample->set_loop_mode(AudioStreamSample::LOOP_DISABLED);
	sample->set_loop_begin(0);
	sample->set_loop_end(0);
	sample->set_stereo(stereo);

	return sample;
}

void AudioEffectRecord::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_should_record", "record"), &AudioEffectRecord::set_should_record);
	ClassDB::bind_method(D_METHOD("get_should_record"), &AudioEffectRecord::get_should_record);
	ClassDB::bind_method(D_METHOD("set_format", "format"), &AudioEffectRecord::set_format);
	ClassDB::bind_method(D_METHOD("get_format"), &AudioEffectRecord::get_format);
	ClassDB::bind_method(D_METHOD("get_recording"), &AudioEffectRecord::get_recording);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "should_record"), "set_should_record", "get_should_record");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "8-Bit,16-Bit,IMA-ADPCM"), "set_format", "get_format");
}

AudioEffectRecord::AudioEffectRecord() {
	format = AudioStreamSample::FORMAT_16_BITS;
}