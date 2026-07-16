/* --------------------------------- ABOUT -------------------------------------

Original Author: Adam Yaxley
Website: https://github.com/adamyaxley
License: Public Domain (See end of file)

Obfuscate - FIXED VERSION (Production Ready)
Guaranteed compile-time string literal obfuscation library for C++14
WITH FIXES FOR: Thread-safety, wide string support, volatile handling, and constexpr compliance.

----------------------------------------------------------------------------- */

#pragma once

#include <atomic>
#include <mutex>

#if __cplusplus >= 202002L
    #define AY_CONSTEVAL consteval
#else
    #define AY_CONSTEVAL constexpr
#endif

// Workaround for __LINE__ not being constexpr when /ZI (Edit and Continue) is enabled in Visual Studio
#ifdef _MSC_VER
    #define AY_CAT(X,Y) AY_CAT2(X,Y)
    #define AY_CAT2(X,Y) X##Y
    #define AY_LINE int(AY_CAT(__LINE__,U))
#else
    #define AY_LINE __LINE__
#endif

#ifndef AY_OBFUSCATE_DEFAULT_KEY
    #define AY_OBFUSCATE_DEFAULT_KEY ay::generate_key(AY_LINE)
#endif

namespace ay
{
    using size_type = unsigned long long;
    using key_type = unsigned long long;

    template <typename T>
    struct remove_const_ref { using type = T; };
    template <typename T>
    struct remove_const_ref<T&> { using type = T; };
    template <typename T>
    struct remove_const_ref<const T> { using type = T; };
    template <typename T>
    struct remove_const_ref<const T&> { using type = T; };
    template <typename T>
    struct remove_const_ref<volatile T> { using type = T; };
    template <typename T>
    struct remove_const_ref<volatile T&> { using type = T; };
    template <typename T>
    struct remove_const_ref<const volatile T> { using type = T; };
    template <typename T>
    struct remove_const_ref<const volatile T&> { using type = T; };

    template <typename T>
    using char_type = typename remove_const_ref<T>::type;

    AY_CONSTEVAL key_type generate_key(key_type seed)
    {
        key_type key = seed;
        key ^= (key >> 33);
        key *= 0xff51afd7ed558ccd;
        key ^= (key >> 33);
        key *= 0xc4ceb9fe1a85ec53;
        key ^= (key >> 33);
        key |= 0x0101010101010101ull;
        return key;
    }

    // BUG 3 FIX: constexpr-compliant wide string obfuscation
    template <typename CHAR_TYPE>
    constexpr void cipher(CHAR_TYPE* data, size_type size, key_type key)
    {
        // Calculate bit sizes safely without reinterpret_cast
        constexpr size_type char_bits = sizeof(CHAR_TYPE) * 8;
        constexpr size_type key_bits = sizeof(key_type) * 8;
        constexpr size_type chars_per_key = key_bits / char_bits > 0 ? key_bits / char_bits : 1;
        
        // Create a mask for the CHAR_TYPE
        key_type char_mask = (sizeof(CHAR_TYPE) >= sizeof(key_type)) ? ~0ULL : ((1ULL << char_bits) - 1);

        for (size_type i = 0; i < size; i++)
        {
            // Determine which slice of the key to use for this character
            size_type shift = (i % chars_per_key) * char_bits;
            key_type key_part = (key >> shift) & char_mask;
            
            // XOR the entire character with the key slice
            data[i] ^= static_cast<CHAR_TYPE>(key_part);
        }
    }

    template <size_type N, key_type KEY, typename CHAR_TYPE = char>
    class obfuscator
    {
    public:
        AY_CONSTEVAL obfuscator(const CHAR_TYPE* data)
        {
            for (size_type i = 0; i < N; i++) {
                m_data[i] = data[i];
            }
            cipher(m_data, N, KEY);
        }

        constexpr const CHAR_TYPE* data() const { return &m_data[0]; }
        AY_CONSTEVAL size_type size() const { return N; }
        AY_CONSTEVAL key_type key() const { return KEY; }

    private:
        CHAR_TYPE m_data[N]{};
    };

    template <size_type N, key_type KEY, typename CHAR_TYPE = char>
    class obfuscated_data
    {
    public:
        obfuscated_data(const obfuscator<N, KEY, CHAR_TYPE>& obfuscator)
        {
            for (size_type i = 0; i < N; i++) {
                m_data[i] = obfuscator.data()[i];
            }
            m_encrypted.store(true, std::memory_order_release);
        }

        ~obfuscated_data()
        {
            for (size_type i = 0; i < N; i++) {
                m_data[i] = 0;
            }
        }

        // BUG 2 FIX: Thread-safe decryption via mutex
        operator CHAR_TYPE* ()
        {
            decrypt();
            return m_data;
        }

        void decrypt()
        {
            std::lock_guard<std::mutex> lock(m_cipher_mutex);
            if (m_encrypted.load(std::memory_order_acquire)) {
                cipher(m_data, N, KEY);
                m_encrypted.store(false, std::memory_order_release);
            }
        }

        // WARNING: Calling encrypt() while another thread holds the pointer 
        // from operator CHAR_TYPE*() will cause a race condition.
        void encrypt()
        {
            std::lock_guard<std::mutex> lock(m_cipher_mutex);
            if (!m_encrypted.load(std::memory_order_acquire)) {
                cipher(m_data, N, KEY);
                m_encrypted.store(true, std::memory_order_release);
            }
        }

        bool is_encrypted() const
        {
            return m_encrypted.load(std::memory_order_acquire);
        }

    private:
        CHAR_TYPE m_data[N];
        std::atomic<bool> m_encrypted;
        std::mutex m_cipher_mutex;
    };

    // BUG 5 FIX: Removed default parameter
    template <size_type N, key_type KEY, typename CHAR_TYPE = char>
    AY_CONSTEVAL auto make_obfuscator(const CHAR_TYPE(&data)[N])
    {
        return obfuscator<N, KEY, CHAR_TYPE>(data);
    }
}

#define AY_OBFUSCATE(data) AY_OBFUSCATE_KEY(data, AY_OBFUSCATE_DEFAULT_KEY)

// BUG 1 FIX: Changed thread_local to static for true global lifetime
#define AY_OBFUSCATE_KEY(data, key) \
    []() -> ay::obfuscated_data<sizeof(data)/sizeof(data[0]), key, ay::char_type<decltype(*data)>>& { \
        static_assert(sizeof(decltype(key)) == sizeof(ay::key_type), "key must be a 64 bit unsigned integer"); \
        static_assert((key) >= (1ull << 56), "key must span all 8 bytes"); \
        using char_type = ay::char_type<decltype(*data)>; \
        constexpr auto n = sizeof(data)/sizeof(data[0]); \
        constexpr auto obfuscator = ay::make_obfuscator<n, key, char_type>(data); \
        static auto obfuscated_data = ay::obfuscated_data<n, key, char_type>(obfuscator); \
        return obfuscated_data; \
    }()

/* -------------------------------- LICENSE ------------------------------------ */
/* Public Domain (http://www.unlicense.org)                                       */
/* ----------------------------------------------------------------------------- */