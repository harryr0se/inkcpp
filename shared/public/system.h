/* Copyright (c) 2024 Julian Benda
 *
 * This file is part of inkCPP which is released under MIT license.
 * See file LICENSE.txt or go to
 * https://github.com/JBenda/inkcpp for full license details.
 */
#pragma once

#include "config.h"

#ifdef INK_ENABLE_UNREAL
#	include "Misc/AssertionMacros.h"
#	include "Misc/CString.h"
#	include "HAL/UnrealMemory.h"
#	include "Hash/CityHash.h"

#endif
#ifdef INK_ENABLE_STL
#	include <exception>
#	include <stdexcept>
#	include <optional>
#	include <cctype>
#	include <cstdint>
#	include <cstdio>
#	include <cstdarg>
#endif

// Platform specific defines //

#ifdef INK_ENABLE_UNREAL
#	define inkZeroMemory(buff, len)        FMemory::Memset(buff, 0, len)
#	define inkAssert(condition, text, ...) checkf(condition, TEXT(text), ##__VA_ARGS__)
#	define inkFail(text, ...)              checkf(false, TEXT(text), ##__VA_ARGS__)
#	define FORMAT_STRING_STR               "%hs"
#else
#	define inkZeroMemory     ink::internal::zero_memory
#	define inkAssert         ink::ink_assert
#	define inkFail(...)      ink::ink_assert(false, __VA_ARGS__)
#	define FORMAT_STRING_STR "%s"
#endif

namespace ink
{
/** define basic numeric type
 * @todo use a less missleading name
 */
typedef unsigned int uint32_t;

/** Name hash (used for temporary variables) */
typedef uint32_t hash_t;

/** Invalid hash value */
const hash_t InvalidHash = 0;

#ifdef INK_ENABLE_UNREAL
/** Simple hash for serialization of strings */
inline hash_t hash_string(const char* string)
{
	return CityHash32(string, FCStringAnsi::Strlen(string));
}
#else
hash_t hash_string(const char* string);
#endif

/** Byte type */
typedef unsigned char byte_t;

/** Used to identify an offset in a data table (like a string in the string table) */
typedef uint32_t offset_t;

/** Instruction pointer used for addressing within the story instructions */
typedef const unsigned char* ip_t;

/** Used for the size of arrays */
typedef unsigned int size_t;

/** Used as the unique identifier for an ink container */
typedef uint32_t container_t;

/** Used to uniquely identify threads */
typedef uint32_t thread_t;

/** Used to unique identify a list flag  */
struct list_flag {
	int16_t list_id;
	int16_t flag;

	bool operator==(const list_flag& o) const { return list_id == o.list_id && flag == o.flag; }

	bool operator!=(const list_flag& o) const { return ! (*this == o); }
};

/** value of an unset list_flag */
constexpr list_flag null_flag{-1, -1};
/** value representing an empty list */
constexpr list_flag empty_flag{-1, 0};

namespace internal
{
	/** Checks if a string starts with a given prefix*/
	static bool starts_with(const char* string, const char* prefix)
	{
		while (*prefix) {
			if (*string != *prefix) {
				return false;
			}
			string++;
			prefix++;
		}
		return true;
	}

	/** Checks if a string is only whitespace*/
	static bool is_whitespace(const char* string, bool includeNewline = true)
	{
		// Iterate string
		while (true) {
			switch (*(string++)) {
				case 0: return true;
				case '\n':
					if (! includeNewline)
						return false;
				case '\t': [[fallthrough]];
				case ' ': continue;
				default: return false;
			}
		}
	}

	/** check if character can be only part of a word, when two part of word characters put together
	 * the will be a space inserted I049
	 */
	inline bool is_part_of_word(char character) { return isalpha(character) || isdigit(character); }

	inline constexpr bool is_whitespace(char character, bool includeNewline = true)
	{
		switch (character) {
			case '\n':
				if (! includeNewline)
					return false;
			case '\t': [[fallthrough]];
			case ' ': return true;
			default: return false;
		}
	}

#ifndef INK_ENABLE_UNREAL
	/** populate memory with Zero */
	void zero_memory(void* buffer, size_t length);
#endif
} // namespace internal

#ifdef INK_ENABLE_STL
/** exception type thrown if something goes wrong */
using ink_exception = std::runtime_error;
#else
// Non-STL exception class
class ink_exception
{
public:
	ink_exception(const char* msg)
	    : _msg(msg)
	{
	}

	inline const char* message() const { return _msg; }

private:
	const char* _msg;
};
#endif

// assert
#ifndef INK_ENABLE_UNREAL
template<typename... Args>
void ink_assert(bool condition, const char* msg = nullptr, Args... args)
{
	static const char* EMPTY = "";
	if (msg == nullptr) {
		msg = EMPTY;
	}
	if (! condition) {
		if constexpr (sizeof...(args) > 0) {
			size_t size    = snprintf(nullptr, 0, msg, args...) + 1;
			char*  message = static_cast<char*>(malloc(size));
			snprintf(message, size, msg, args...);
			throw ink_exception(message);
		} else {
			throw ink_exception(msg);
		}
	}
}

template<typename... Args>
[[noreturn]] inline void ink_assert(const char* msg = nullptr, Args... args)
{
	ink_assert(false, msg, args...);
	exit(EXIT_FAILURE);
}
#endif

namespace runtime::internal
{
	constexpr unsigned abs(int i) { return i < 0 ? -i : i; }

	template<typename T>
	struct always_false {
		static constexpr bool value = false;
	};

	template<bool Con, typename T1, typename T2>
	struct if_type {
		using type = T1;
	};

	template<typename T1, typename T2>
	struct if_type<false, T1, T2> {
		using type = T2;
	};

	template<bool Con, typename T1, typename T2>
	using if_t = typename if_type<Con, T1, T2>::type;

	template<bool Enable, typename T = void>
	struct enable_if {
	};

	template<typename T>
	struct enable_if<true, T> {
		using type = T;
	};

	template<bool Enable, typename T = void>
	using enable_if_t = typename enable_if<Enable, T>::type;
} // namespace runtime::internal


#ifdef INK_ENABLE_STL
/** custom optional implementation for usage if STL is disabled
 * @tparam T type contaied in optional
 */
template<typename T>
using optional                   = std::optional<T>;
/** an empty #optional */
constexpr std::nullopt_t nullopt = std::nullopt;
#else
struct nullopt_t {
};

constexpr nullopt_t nullopt;

template<typename T>
class optional
{
public:
	optional() {}

	optional(nullopt_t) {}

	optional(T&& val)
	    : _has_value{true}
	    , _value{val}
	{
	}

	optional(const T& val)
	    : _has_value{true}
	    , _value{val}
	{
	}

	const T& operator*() const { return _value; }

	T& operator*() { return _value; }

	const T* operator->() const { return &_value; }

	T* operator->() { return &_value; }

	constexpr bool has_value() const { return _has_value; }

	constexpr T& value()
	{
		test_value();
		return _value;
	}

	constexpr const T& value() const
	{
		test_value();
		return _value;
	}

	constexpr operator bool() const { return has_value(); }

	template<typename U>
	constexpr T value_or(U&& u) const
	{
		return _has_value ? _value : static_cast<T>(u);
	}

	template<typename... Args>
	T& emplace(Args... args)
	{
		_value.~T();
		return *(new (&_value) T(args...));
	}

private:
	void test_value() const
	{
		if (! _has_value) {
			inkFail("Can't access empty optional!");
		}
	}

	bool _has_value = false;
	T    _value;
};
#endif
} // namespace ink
