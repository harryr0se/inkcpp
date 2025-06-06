/* Copyright (c) 2024 Julian Benda
 *
 * This file is part of inkCPP which is released under MIT license.
 * See file LICENSE.txt or go to
 * https://github.com/JBenda/inkcpp for full license details.
 */
#pragma once

#include "snapshot.h"
#include <cstring>

namespace ink::runtime::internal
{
class globals_impl;
template<typename, bool, size_t>
class managed_array;
class snap_tag;
class string_table;
class value;

class snapshot_interface
{
public:
	constexpr snapshot_interface(){};

	static unsigned char* snap_write(unsigned char* ptr, const void* data, size_t length, bool write)
	{
		if (write) {
			memcpy(ptr, data, length);
		}
		return ptr + length;
	}

	template<typename T>
	static unsigned char* snap_write(unsigned char* ptr, const T& data, bool write)
	{
		return snap_write(ptr, &data, sizeof(data), write);
	}

	static const unsigned char* snap_read(const unsigned char* ptr, void* data, size_t length)
	{
		memcpy(data, ptr, length);
		return ptr + length;
	}

	template<typename T>
	static const unsigned char* snap_read(const unsigned char* ptr, T& data)
	{
		return snap_read(ptr, &data, sizeof(data));
	}

	struct snapper {
		const string_table& strings;
		const char*         story_string_table;
		const snap_tag*     runner_tags = nullptr;
	};

	struct loader {
		managed_array<const char*, true, 5>& string_table; /// FIXME: make configurable
		const char*                          story_string_table;
		const snap_tag*                      runner_tags = nullptr;
	};

	size_t snap(unsigned char* data, snapper&) const
	{
		inkFail("Snap function not implemented");
		return 0;
	};

	const unsigned char* snap_load(const unsigned char* data, loader&)
	{
		inkFail("Snap function not implemented");
		return nullptr;
	};
};
} // namespace ink::runtime::internal
