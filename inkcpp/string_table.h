/* Copyright (c) 2024 Julian Benda
 *
 * This file is part of inkCPP which is released under MIT license.
 * See file LICENSE.txt or go to
 * https://github.com/JBenda/inkcpp for full license details.
 */
#pragma once

#include "avl_array.h"
#include "config.h"
#include "system.h"
#include "snapshot_impl.h"

namespace ink::runtime::internal
{
// hash tree sorted by string pointers
class string_table final : public snapshot_interface
{
public:
	virtual ~string_table();

	// Create a dynamic string of a particular length
	char* create(size_t length);
	char* duplicate(const char* str);

	// zeroes all usage values
	void clear_usage();

	// mark a string as used
	void mark_used(const char* string);


	// snapshot interface implementation
	size_t               snap(unsigned char* data, const snapper&) const;
	const unsigned char* snap_load(const unsigned char* data, const loader&);

	// get position of string when iterate through data
	// used to enable storing a string table references
	size_t get_id(const char* string) const;

	// deletes all unused strings
	void gc();

	/** Get usage statistics for the string_table. */
	config::statistics::string_table statistics() const;

private:
	avl_array < const char*, bool, ink::size_t,
	    config::limitStringTable<0, abs(config::limitStringTable)> _table;
	static constexpr const char*                                   EMPTY_STRING = "\x03";
};
} // namespace ink::runtime::internal
