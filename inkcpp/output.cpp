/* Copyright (c) 2024 Julian Benda
 *
 * This file is part of inkCPP which is released under MIT license.
 * See file LICENSE.txt or go to
 * https://github.com/JBenda/inkcpp for full license details.
 */
#include "output.h"
#include "string_table.h"
#include "list_table.h"
#include <system.h>
#include "string_utils.h"

#ifdef INK_ENABLE_STL
#	include <iomanip>
#endif

namespace ink::runtime::internal
{
basic_stream::basic_stream(value* buffer, size_t len)
    : _data(buffer)
    , _max(len)
{
}

void basic_stream::initelize_data(value* buffer, size_t size)
{
	inkAssert(
	    _data == nullptr && _max == 0,
	    "Try to double initialize a basic_stream."
	    "To extend the size use overflow()"
	);
	_data = buffer;
	_max  = size;
}

void basic_stream::overflow(value*& buffer, size_t& size, size_t target)
{
	inkFail("Stack overflow!");
}

void basic_stream::append(const value& in)
{
	// newline after glue -> no newline
	// newline after glue
	// SPECIAL: Incoming newline
	if (in.type() == value_type::newline && _size > 1) {
		// If the end of the stream is a function start marker, we actually
		//  want to ignore this. Function start trimming.
		if (_data[_size - 1].type() == value_type::func_start)
			return;
		size_t i = _size - 1;
		while (true) {
			value& d = _data[i];
			// ignore additional newlines after newline or glue
			if (d.type() == value_type::newline || d.type() == value_type::glue) {
				return;
			} else if (d.type() == value_type::string
			           && ink::internal::is_whitespace(d.get<value_type::string>())) {
			} else if (d.type() == value_type::func_start || d.type() == value_type::func_end) {
			} else {
				break;
			}
			if (i == 0) {
				return;
			}
			--i;
		}
	}

	// Ignore leading newlines
	if (in.type() == value_type::newline && _size == 0)
		return;

	// Add to data stream
	if (_size >= _max) {
		overflow(_data, _max);
	}
	_data[_size++] = in;

	// Special: Incoming glue. Trim whitespace/newlines prior
	//  This also applies when a function ends to trim trailing whitespace.
	if ((in.type() == value_type::glue || in.type() == value_type::func_end) && _size > 1) {
		// Run backwards
		size_t i            = _size - 2;
		int    func_end_cnt = 0;
		while (true) {
			value& d = _data[i];

			// Nullify newlines
			if (d.type() == value_type::newline) {
				d = value{};
			}

			// Nullify whitespace
			else if (d.type() == value_type::string
			         && ::ink::internal::is_whitespace(d.get<value_type::string>()))
				d = value{};
			else if (d.type() == value_type::func_end) {
				++func_end_cnt;
			} else if (d.type() == value_type::func_start && func_end_cnt > 0) {
				--func_end_cnt;
			}

			// If it's not a newline or whitespace, stop
			else
				break;

			// If we've hit the end, break
			if (i == 0)
				break;

			// Move on to next element
			i--;
		}
	}
}

void basic_stream::append(const value* in, unsigned int length)
{
	// TODO: Better way to bulk while still executing glue checks?
	for (size_t i = 0; i < length; i++)
		append(in[i]);
}

template<typename T>
inline void write_char(T& output, char c)
{
	static_assert(always_false<T>::value, "Invalid output type");
}

template<>
inline void write_char(char*& output, char c)
{
	(*output++) = c;
}

#ifdef INK_ENABLE_STL
template<>
inline void write_char(std::stringstream& output, char c)
{
	output.put(c);
}
#endif

inline bool get_next(const value* list, size_t i, size_t size, const value** next)
{
	while (i + 1 < size) {
		*next           = &list[i + 1];
		value_type type = (*next)->type();
		if ((*next)->printable()) {
			return true;
		}
		i++;
	}

	return false;
}

template<typename T>
void basic_stream::copy_string(const char* str, size_t& dataIter, T& output)
{
	while (*str != 0) {
		write_char(output, *str++);
	}
}

#ifdef INK_ENABLE_STL
std::string basic_stream::get()
{
	size_t start = find_start();

	// Move up from marker
	bool              hasGlue = false, lastNewline = false;
	std::stringstream str;
	for (size_t i = start; i < _size; i++) {
		if (should_skip(i, hasGlue, lastNewline))
			continue;
		if (_data[i].printable()) {
			_data[i].write(str, _lists_table);
		}
	}

	// Reset stream size to where we last held the marker
	_size = start;

	// Return processed string
	// remove mulitple accourencies of ' '
	std::string result = str.str();
	if (! result.empty()) {
		auto end = clean_string<true, false>(result.begin(), result.end());
		if (result.begin() == end) {
			result.resize(0);
		} else {
			_last_char = *(end - 1);
			result.resize(end - result.begin() - (_last_char == ' ' ? 1 : 0));
		}
	}
	return result;
}
#endif
#ifdef INK_ENABLE_UNREAL
FString basic_stream::get()
{
	UE_LOG(
	    InkCpp, Warning,
	    TEXT("Basic stream::get is not implemented correctly and should not be used implemented "
	         "correctly!")
	);
	FString str;
	return str;
}
#endif

size_t basic_stream::queued() const
{
	size_t start = find_start();
	return _size - start;
}

const value& basic_stream::peek() const
{
	inkAssert(_size > 0, "Attempting to peek empty stream!");
	return _data[_size - 1];
}

void basic_stream::discard(size_t length)
{
	// Protect against size underflow
	_size -= std::min(length, _size);
}

void basic_stream::get(value* ptr, size_t length)
{
	// Find start
	size_t start = find_start();

	const value* end = ptr + length;
	// inkAssert(_size - start < length, "Insufficient space in data array to store stream
	// contents!");

	// Move up from marker
	bool hasGlue = false, lastNewline = false;
	for (size_t i = start; i < _size; i++) {
		if (should_skip(i, hasGlue, lastNewline))
			continue;

		// Make sure we can fit the next element
		inkAssert(ptr < end, "Insufficient space in data array to store stream contents!");

		// Copy any value elements
		if (_data[i].printable()) {
			*(ptr++) = _data[i];
		}
	}

	// Reset stream size to where we last held the marker
	_size = start;
}

size_t basic_stream::find_first_of(value_type type, size_t offset /*= 0*/) const
{
	if (_size == 0)
		return npos;

	// TODO: Cache?
	for (size_t i = offset; i < _size; ++i) {
		if (_data[i].type() == type)
			return i;
	}

	return npos;
}

size_t basic_stream::find_last_of(value_type type, size_t offset /*= 0*/) const
{
	if (_size == 0)
		return -1;

	// Special case to make the reverse loop easier
	if (_size == 1 && offset == 0)
		return (_data[0].type() == type) ? 0 : npos;

	for (size_t i = _size - 1; i > offset; --i) {
		if (_data[i].type() == type)
			return i;
	}

	return npos;
}

bool basic_stream::ends_with(value_type type, size_t offset /*= npos*/) const
{
	if (_size == 0)
		return false;

	const size_t index = (offset != npos) ? offset - 1 : _size - 1;
	return (index < _size) ? _data[index].type() == type : false;
}

void basic_stream::save()
{
	inkAssert(! saved(), "Can not save over existing save point!");

	// Save the current size
	_save = _size;
}

void basic_stream::restore()
{
	inkAssert(saved(), "No save point to restore!");

	// Restore size to saved position
	_size = _save;
	_save = npos;
}

void basic_stream::forget()
{
	// Just null the save point and continue as normal
	_save = npos;
}

template char* basic_stream::get_alloc<true>(string_table& strings, list_table& lists);
template char* basic_stream::get_alloc<false>(string_table& strings, list_table& lists);

template<bool RemoveTail>
char* basic_stream::get_alloc(string_table& strings, list_table& lists)
{
	size_t start = find_start();

	// Two passes. First for length
	size_t length  = 0;
	bool   hasGlue = false, lastNewline = false;
	for (size_t i = start; i < _size; i++) {
		if (should_skip(i, hasGlue, lastNewline))
			continue;
		++length; // potenzial space to sperate
		if (_data[i].printable()) {
			switch (_data[i].type()) {
				case value_type::list: length += lists.stringLen(_data[i].get<value_type::list>()); break;
				case value_type::list_flag:
					length += lists.stringLen(_data[i].get<value_type::list_flag>());
					break;
				default: length += value_length(_data[i]);
			}
		}
	}

	// Allocate
	char* buffer = strings.create(length + 1);
	char* end    = buffer + length + 1;
	char* ptr    = buffer;
	hasGlue      = false;
	lastNewline  = false;
	for (size_t i = start; i < _size; i++) {
		if (should_skip(i, hasGlue, lastNewline))
			continue;
		if (! _data[i].printable()) {
			continue;
		}
		switch (_data[i].type()) {
			case value_type::int32:
			case value_type::float32:
			case value_type::uint32:
				// Convert to string and advance
				toStr(ptr, end - ptr, _data[i]);
				while (*ptr != 0)
					ptr++;

				break;
			case value_type::string: {
				// Copy string and advance
				const char* value = _data[i].get<value_type::string>();
				copy_string(value, i, ptr);
			} break;
			case value_type::newline:
				*ptr = '\n';
				ptr++;
				break;
			case value_type::list: ptr = lists.toString(ptr, _data[i].get<value_type::list>()); break;
			case value_type::list_flag:
				ptr = lists.toString(ptr, _data[i].get<value_type::list>());
				break;
			default: inkFail("cant convert expression to string!");
		}
	}

	// Make sure last character is a null
	*ptr = 0;

	// Reset stream size to where we last held the marker
	_size = start;

	// Return processed string
	end  = clean_string<true, false>(buffer, buffer + c_str_len(buffer));
	*end = 0;
	if (end != buffer) {
		_last_char = end[-1];
		if constexpr (RemoveTail) {
			if (_last_char == ' ') {
				end[-1] = 0;
			}
		}
	} else {
		_last_char = 'e';
	}

	return buffer;
}

size_t basic_stream::find_start() const
{
	// Find marker (or start)
	size_t start = _size;
	while (start > 0) {
		start--;
		if (_data[start].type() == value_type::marker)
			break;
	}

	// Make sure we're not violating a save point
	if (saved() && start < _save) {
		// TODO: check if we don't reset save correct
		// at some point we can modifiy the output even behind save (probally discard?) and push a new
		// element -> invalid save point
		inkAssert(false, "Trying to access output stream prior to save point!");
	}

	return start;
}

bool basic_stream::should_skip(size_t iter, bool& hasGlue, bool& lastNewline) const
{
	if (_data[iter].printable() && _data[iter].type() != value_type::newline
	    && _data[iter].type() != value_type::string) {
		lastNewline = false;
		hasGlue     = false;
	} else {
		switch (_data[iter].type()) {
			case value_type::newline:
				if (lastNewline)
					return true;
				if (hasGlue)
					return true;
				lastNewline = true;
				break;
			case value_type::glue: hasGlue = true; break;
			case value_type::string: {
				lastNewline = false;
				// an empty string don't count as glued I095
				for (const char* i = _data[iter].get<value_type::string>(); *i; ++i) {
					// isspace only supports characters in [0, UCHAR_MAX]
					if (! isspace(static_cast<unsigned char>(*i))) {
						hasGlue = false;
						break;
					}
				}
			} break;
			default: break;
		}
	}

	return false;
}

bool basic_stream::text_past_save() const
{
	// Check if there is text past the save
	for (size_t i = _save; i < _size; i++) {
		const value& d = _data[i];
		if (d.type() == value_type::string) {
			// TODO: Cache what counts as whitespace?
			if (! ink::internal::is_whitespace(d.get<value_type::string>(), false))
				return true;
		} else if (d.printable()) {
			return true;
		} else if (d.type() == value_type::null) {
			return true;
		}
	}

	// No text
	return false;
}

void basic_stream::clear()
{
	_save = npos;
	_size = 0;
}

void basic_stream::mark_used(string_table& strings, list_table& lists) const
{
	// Find all allocated strings and mark them as used
	for (size_t i = 0; i < _size; i++) {
		if (_data[i].type() == value_type::string) {
			string_type str = _data[i].get<value_type::string>();
			if (str.allocated) {
				strings.mark_used(str.str);
			}
		} else if (_data[i].type() == value_type::list) {
			lists.mark_used(_data[i].get<value_type::list>());
		}
	}
}

#ifdef INK_ENABLE_STL
std::ostream& operator<<(std::ostream& out, basic_stream& in)
{
	out << in.get();
	return out;
}

basic_stream& operator>>(basic_stream& in, std::string& out)
{
	out = in.get();
	return in;
}
#endif
#ifdef INK_ENABLE_UNREAL
basic_stream& operator>>(basic_stream& in, FString& out)
{
	out = in.get();
	return in;
}
#endif

size_t basic_stream::snap(unsigned char* data, const snapper& snapper) const
{
	unsigned char* ptr = data;
	ptr                = snap_write(ptr, _last_char, data != nullptr);
	ptr                = snap_write(ptr, _size, data != nullptr);
	ptr                = snap_write(ptr, _save, data != nullptr);
	for (auto itr = _data; itr != _data + _size; ++itr) {
		ptr += itr->snap(data ? ptr : nullptr, snapper);
	}
	return ptr - data;
}

const unsigned char* basic_stream::snap_load(const unsigned char* ptr, const loader& loader)
{
	ptr = snap_read(ptr, _last_char);
	ptr = snap_read(ptr, _size);
	ptr = snap_read(ptr, _save);
	if (_size >= _max) {
		overflow(_data, _max, _size);
	}
	inkAssert(_max >= _size, "output is to small to hold stored data");
	for (auto itr = _data; itr != _data + _size; ++itr) {
		ptr = itr->snap_load(ptr, loader);
	}
	return ptr;
}

} // namespace ink::runtime::internal
