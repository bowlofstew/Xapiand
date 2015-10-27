/*
 * Copyright (C) 2015 deipi.com LLC and contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "xapiand.h"

#include <string>
#include <vector>


struct VolumeError : std::exception {};


class HaystackVolume
{
	struct NeedleHeader {
		uint64_t magic; // Magic number used to find the next possible needle during recovery
		uint64_t cookie;  // Security cookie supplied by client to prevent brute force attacks
		size_t size;  // Full size (uncompressed)
		// uint32_t chunk_size
		// data goes here...
	};

	struct NeedleFooter {
		// uint32_t zero
		uint64_t magic;  // Magic number used to find possible needle end during recovery
		uint64_t checksum;  // Checksum of the data portion of the needle
		// padding to align total needle size to 8 bytes
	};

	uint32_t offset;

	std::string data_path;
	int data_file;

	void write_header(uint64_t cookie, size_t size);
	ssize_t write_chunk(const char *data, size_t size);
	uint32_t write_footer(size_t total_size, uint64_t checksum);

public:
	HaystackVolume(const std::string &path, bool writable);
	~HaystackVolume();

	uint32_t get_offset();

	ssize_t read_data(uint32_t offset, uint64_t cookie, char *data, size_t size);
	ssize_t write_data(uint64_t cookie, const char *data, size_t size);
};


class HaystackIndex
{
	std::string index_path;
	int index_file;

	uint32_t index_base;
	std::vector<uint32_t> index;

public:
	HaystackIndex(const std::string &path, bool writable);
	~HaystackIndex();

	uint32_t get_offset(uint32_t docid);
	void set_offset(uint32_t docid, uint32_t offset);
};


class Haystack
{
	std::unique_ptr<HaystackIndex> index;
	std::unique_ptr<HaystackVolume> volume;

public:
	Haystack(const std::string &path, bool writable=false);

	ssize_t read(uint32_t docid, uint64_t cookie, char *data, size_t size);
	ssize_t write(uint32_t docid, uint64_t cookie, const char *data, size_t size);
};
