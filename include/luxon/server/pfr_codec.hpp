// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <array>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <concepts>
#include <cstddef>
#include <cmath>
#include <boost/pfr.hpp>
#include <luxon/ser_types.hpp>

namespace server::pfr_codec {

using namespace luxon::ser;

// -----------------------------------------------------------------------------
// public API
// -----------------------------------------------------------------------------

struct options {
    std::size_t max_depth = 64;
};

struct error {
    std::string path{"$"};
    std::string message{};
};

template <typename T> using result = std::expected<T, error>;

template <typename T>
[[nodiscard]]
result<Value> to_value(const T& x, const options& opt = {});

template <typename T>
[[nodiscard]]
result<T> from_value(const Value& v, const options& opt = {});

template <typename T>
[[nodiscard]]
bool load(const Value& v, T& out, const options& opt = {}) {
    auto r = from_value<T>(v, opt);
    if (!r)
        return false;
    out = std::move(*r);
    return true;
}

// -----------------------------------------------------------------------------
// implementation
// -----------------------------------------------------------------------------

namespace detail {

template <typename T> using clean_t = std::remove_cvref_t<T>;

template <typename> struct always_false : std::false_type {};

template <typename T> struct is_optional : std::false_type {};
template <typename U> struct is_optional<std::optional<U>> : std::true_type {};
template <typename T> inline constexpr bool is_optional_v = is_optional<clean_t<T>>::value;

template <typename T> struct is_std_vector : std::false_type {};
template <typename U, typename A> struct is_std_vector<std::vector<U, A>> : std::true_type {};
template <typename T> inline constexpr bool is_std_vector_v = is_std_vector<clean_t<T>>::value;

template <typename T> struct is_std_array : std::false_type {};
template <typename U, std::size_t N> struct is_std_array<std::array<U, N>> : std::true_type {};
template <typename T> inline constexpr bool is_std_array_v = is_std_array<clean_t<T>>::value;

template <typename T> struct is_std_pair : std::false_type {};
template <typename A, typename B> struct is_std_pair<std::pair<A, B>> : std::true_type {};
template <typename T> inline constexpr bool is_std_pair_v = is_std_pair<clean_t<T>>::value;

template <typename T> struct is_std_tuple : std::false_type {};
template <typename... Ts> struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_std_tuple_v = is_std_tuple<clean_t<T>>::value;

template <typename T> struct is_std_variant : std::false_type {};
template <typename... Ts> struct is_std_variant<std::variant<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_std_variant_v = is_std_variant<clean_t<T>>::value;

template <typename T> struct is_map_like : std::false_type {};
template <typename K, typename V, typename C, typename A> struct is_map_like<std::map<K, V, C, A>> : std::true_type {};
template <typename K, typename V, typename H, typename E, typename A> struct is_map_like<std::unordered_map<K, V, H, E, A>> : std::true_type {};
template <typename T> inline constexpr bool is_map_like_v = is_map_like<clean_t<T>>::value;

template <typename T> struct is_exact_native : std::false_type {};

template <> struct is_exact_native<std::monostate> : std::true_type {};
template <> struct is_exact_native<bool> : std::true_type {};
template <> struct is_exact_native<uint8_t> : std::true_type {};
template <> struct is_exact_native<int16_t> : std::true_type {};
template <> struct is_exact_native<int32_t> : std::true_type {};
template <> struct is_exact_native<int64_t> : std::true_type {};
template <> struct is_exact_native<float> : std::true_type {};
template <> struct is_exact_native<double> : std::true_type {};
template <> struct is_exact_native<std::string> : std::true_type {};
template <> struct is_exact_native<std::vector<uint8_t>> : std::true_type {};
template <> struct is_exact_native<std::vector<bool>> : std::true_type {};
template <> struct is_exact_native<std::vector<int16_t>> : std::true_type {};
template <> struct is_exact_native<std::vector<int32_t>> : std::true_type {};
template <> struct is_exact_native<std::vector<int64_t>> : std::true_type {};
template <> struct is_exact_native<std::vector<float>> : std::true_type {};
template <> struct is_exact_native<std::vector<double>> : std::true_type {};
template <> struct is_exact_native<std::vector<std::string>> : std::true_type {};
template <> struct is_exact_native<std::vector<Value>> : std::true_type {};
template <> struct is_exact_native<JaggedArray> : std::true_type {};
template <> struct is_exact_native<Dictionary> : std::true_type {};
template <> struct is_exact_native<GenericDictionary> : std::true_type {};
template <> struct is_exact_native<HashtablePtr> : std::true_type {};
template <> struct is_exact_native<RawCustomValue> : std::true_type {};
template <> struct is_exact_native<EventMessage> : std::true_type {};
template <> struct is_exact_native<OperationRequestMessage> : std::true_type {};
template <> struct is_exact_native<OperationResponseMessage> : std::true_type {};
template <> struct is_exact_native<std::vector<Dictionary>> : std::true_type {};
template <> struct is_exact_native<std::vector<GenericDictionary>> : std::true_type {};
template <> struct is_exact_native<std::vector<HashtablePtr>> : std::true_type {};
template <> struct is_exact_native<std::vector<RawCustomValue>> : std::true_type {};
template <> struct is_exact_native<PreSerializedValue> : std::true_type {};

template <typename T> inline constexpr bool is_exact_native_v = is_exact_native<clean_t<T>>::value;

template <typename T>
concept pfr_reflectable = std::is_class_v<clean_t<T>> && requires { boost::pfr::tuple_size_v<clean_t<T>>; };

inline std::string child_path(std::string_view base, std::size_t index) {
    std::string out(base);
    out.push_back('[');
    out += std::to_string(index);
    out.push_back(']');
    return out;
}

inline std::string_view value_kind(const Value& v) {
    static constexpr std::array<std::string_view, 31> names{
        "null",
        "bool",
        "byte",
        "int16",
        "int32",
        "int64",
        "float",
        "double",
        "string",
        "bytes",
        "bool[]",
        "int16[]",
        "int32[]",
        "int64[]",
        "float[]",
        "double[]",
        "string[]",
        "object[]",
        "jagged[]",
        "dictionary",
        "generic_dictionary",
        "hashtable",
        "custom",
        "event",
        "operation_request",
        "operation_response",
        "dictionary[]",
        "generic_dictionary[]",
        "hashtable[]",
        "custom[]",
        "pre_serialized",
    };
    return names[v.value.index()];
}

inline error make_error(std::string_view path, std::string message) { return error{std::string(path), std::move(message)}; }

template <typename T> result<T> fail(std::string_view path, std::string message) { return std::unexpected(make_error(path, std::move(message))); }

template <typename T> result<T> type_error(std::string_view path, std::string_view expected, const Value& v) {
    return fail<T>(path, std::string("expected ") + std::string(expected) + ", got " + std::string(value_kind(v)));
}

inline const ObjectArray *as_list_ptr(const Value& v) {
    if (auto p = v.get_ptr<ObjectArray>())
        return p;
    if (auto p = v.get_ptr<JaggedArray>())
        return &p->elements;
    return nullptr;
}

template <typename T> result<Value> encode(const T& x, const options& opt, std::string_view path, std::size_t depth);

template <typename T> result<T> decode(const Value& v, const options& opt, std::string_view path, std::size_t depth);

template <typename Elem, typename It>
result<Value> encode_range(It first, It last, std::size_t n, const options& opt, std::string_view path, std::size_t depth) {
    using U = clean_t<Elem>;

    if constexpr (std::same_as<U, std::byte>) {
        ByteArray out;
        out.reserve(n);
        for (; first != last; ++first)
            out.push_back(std::to_integer<uint8_t>(*first));
        return Value(std::move(out));
    } else if constexpr (std::same_as<U, uint8_t>) {
        return Value(ByteArray(first, last));
    } else if constexpr (std::same_as<U, bool>) {
        return Value(std::vector<bool>(first, last));
    } else if constexpr (std::same_as<U, int16_t>) {
        return Value(std::vector<int16_t>(first, last));
    } else if constexpr (std::same_as<U, int32_t>) {
        return Value(std::vector<int32_t>(first, last));
    } else if constexpr (std::same_as<U, int64_t>) {
        return Value(std::vector<int64_t>(first, last));
    } else if constexpr (std::same_as<U, float>) {
        return Value(std::vector<float>(first, last));
    } else if constexpr (std::same_as<U, double>) {
        return Value(std::vector<double>(first, last));
    } else if constexpr (std::same_as<U, std::string>) {
        return Value(std::vector<std::string>(first, last));
    } else if constexpr (std::same_as<U, Value>) {
        return Value(ObjectArray(first, last));
    } else if constexpr (std::same_as<U, Dictionary>) {
        return Value(std::vector<Dictionary>(first, last));
    } else if constexpr (std::same_as<U, GenericDictionary>) {
        return Value(std::vector<GenericDictionary>(first, last));
    } else if constexpr (std::same_as<U, HashtablePtr>) {
        return Value(std::vector<HashtablePtr>(first, last));
    } else if constexpr (std::same_as<U, Hashtable>) {
        std::vector<HashtablePtr> out;
        out.reserve(n);
        for (; first != last; ++first)
            out.push_back(std::make_shared<Hashtable>(*first));
        return Value(std::move(out));
    } else if constexpr (std::same_as<U, RawCustomValue>) {
        return Value(std::vector<RawCustomValue>(first, last));
    } else {
        ObjectArray out;
        out.reserve(n);
        std::size_t i = 0;
        for (; first != last; ++first, ++i) {
            auto r = encode(*first, opt, child_path(path, i), depth + 1);
            if (!r)
                return std::unexpected(std::move(r.error()));
            out.emplace_back(std::move(*r));
        }
        return Value(std::move(out));
    }
}

template <typename T> std::optional<result<std::vector<T>>> try_direct_vector(const Value& v, std::string_view path) {
    if constexpr (std::same_as<T, std::byte>) {
        if (auto p = v.get_ptr<ByteArray>()) {
            std::vector<std::byte> out;
            out.reserve(p->size());
            for (auto b : *p)
                out.push_back(static_cast<std::byte>(b));
            return out;
        }
    } else if constexpr (std::same_as<T, uint8_t>) {
        if (auto p = v.get_ptr<ByteArray>())
            return *p;
    } else if constexpr (std::same_as<T, bool>) {
        if (auto p = v.get_ptr<std::vector<bool>>())
            return *p;
    } else if constexpr (std::same_as<T, int16_t>) {
        if (auto p = v.get_ptr<std::vector<int16_t>>())
            return *p;
    } else if constexpr (std::same_as<T, int32_t>) {
        if (auto p = v.get_ptr<std::vector<int32_t>>())
            return *p;
    } else if constexpr (std::same_as<T, int64_t>) {
        if (auto p = v.get_ptr<std::vector<int64_t>>())
            return *p;
    } else if constexpr (std::same_as<T, float>) {
        if (auto p = v.get_ptr<std::vector<float>>())
            return *p;
    } else if constexpr (std::same_as<T, double>) {
        if (auto p = v.get_ptr<std::vector<double>>())
            return *p;
    } else if constexpr (std::same_as<T, std::string>) {
        if (auto p = v.get_ptr<std::vector<std::string>>())
            return *p;
    } else if constexpr (std::same_as<T, Value>) {
        if (auto p = as_list_ptr(v))
            return *p;
    } else if constexpr (std::same_as<T, Dictionary>) {
        if (auto p = v.get_ptr<std::vector<Dictionary>>())
            return *p;
    } else if constexpr (std::same_as<T, GenericDictionary>) {
        if (auto p = v.get_ptr<std::vector<GenericDictionary>>())
            return *p;
    } else if constexpr (std::same_as<T, HashtablePtr>) {
        if (auto p = v.get_ptr<std::vector<HashtablePtr>>())
            return *p;
    } else if constexpr (std::same_as<T, Hashtable>) {
        if (auto p = v.get_ptr<std::vector<HashtablePtr>>()) {
            std::vector<Hashtable> out;
            out.reserve(p->size());
            std::size_t i = 0;
            for (auto const& hp : *p) {
                if (!hp)
                    return fail<std::vector<Hashtable>>(child_path(path, i), "null hashtable pointer");
                out.push_back(*hp);
                ++i;
            }
            return out;
        }
    } else if constexpr (std::same_as<T, RawCustomValue>) {
        if (auto p = v.get_ptr<std::vector<RawCustomValue>>())
            return *p;
    }
    return std::nullopt;
}

template <typename Array, typename Vec, std::size_t... I> Array vector_to_array(Vec& vec, std::index_sequence<I...>) {
    using E = typename Array::value_type;
    if constexpr (std::same_as<E, bool>) {
        return Array{static_cast<bool>(vec[I])...};
    } else {
        return Array{std::move(vec[I])...};
    }
}

template <typename T, std::size_t... I>
result<Value> encode_tuple_impl(const T& x, const options& opt, std::string_view path, std::size_t depth, std::index_sequence<I...>) {
    ObjectArray out;
    out.reserve(sizeof...(I));

    error err{};
    bool ok = true;

    (([&] {
         if (!ok)
             return;
         auto r = encode(std::get<I>(x), opt, child_path(path, I), depth + 1);
         if (!r) {
             err = std::move(r.error());
             ok = false;
             return;
         }
         out.emplace_back(std::move(*r));
     }()),
     ...);

    if (!ok)
        return std::unexpected(std::move(err));
    return Value(std::move(out));
}

template <typename T, std::size_t... I>
result<Value> encode_pfr_impl(const T& x, const options& opt, std::string_view path, std::size_t depth, std::index_sequence<I...>) {
    ObjectArray out;
    out.reserve(sizeof...(I));

    error err{};
    bool ok = true;

    (([&] {
         if (!ok)
             return;
         auto r = encode(boost::pfr::get<I>(x), opt, child_path(path, I), depth + 1);
         if (!r) {
             err = std::move(r.error());
             ok = false;
             return;
         }
         out.emplace_back(std::move(*r));
     }()),
     ...);

    if (!ok)
        return std::unexpected(std::move(err));
    return Value(std::move(out));
}

template <typename Tuple, std::size_t... I>
result<Tuple> decode_tuple_impl(const ObjectArray& arr, const options& opt, std::string_view path, std::size_t depth, std::index_sequence<I...>) {
    static_assert((!std::is_array_v<std::tuple_element_t<I, Tuple>> && ...), "raw C arrays inside tuples are not supported; use std::array");

    std::tuple<std::optional<std::tuple_element_t<I, Tuple>>...> tmp;
    std::optional<error> err;

    (([&] {
         if (err)
             return;
         using E = std::tuple_element_t<I, Tuple>;
         auto r = decode<E>(arr[I], opt, child_path(path, I), depth + 1);
         if (!r) {
             err = std::move(r.error());
             return;
         }
         std::get<I>(tmp).emplace(std::move(*r));
     }()),
     ...);

    if (err)
        return std::unexpected(std::move(*err));
    return Tuple{std::move(*std::get<I>(tmp))...};
}

template <typename T, std::size_t I> using pfr_field_t = std::remove_cv_t<std::remove_reference_t<decltype(boost::pfr::get<I>(std::declval<T&>()))>>;

template <typename T, std::size_t... I>
result<T> decode_pfr_impl(const ObjectArray& arr, const options& opt, std::string_view path, std::size_t depth, std::index_sequence<I...>) {
    static_assert((!std::is_array_v<pfr_field_t<T, I>> && ...), "raw C arrays inside reflected structs are not supported; use std::array");

    std::tuple<std::optional<pfr_field_t<T, I>>...> tmp;
    std::optional<error> err;

    (([&] {
         if (err)
             return;
         using E = pfr_field_t<T, I>;
         auto r = decode<E>(arr[I], opt, child_path(path, I), depth + 1);
         if (!r) {
             err = std::move(r.error());
             return;
         }
         std::get<I>(tmp).emplace(std::move(*r));
     }()),
     ...);

    if (err)
        return std::unexpected(std::move(*err));
    return T{std::move(*std::get<I>(tmp))...};
}

template <typename Variant, std::size_t I = 0>
result<Variant> decode_variant_alt(std::size_t index, const Value& v, const options& opt, std::string_view path, std::size_t depth) {
    if constexpr (I >= std::variant_size_v<Variant>) {
        return fail<Variant>(path, "variant index out of range");
    } else {
        if (index == I) {
            using Alt = std::variant_alternative_t<I, Variant>;
            auto r = decode<Alt>(v, opt, path, depth);
            if (!r)
                return std::unexpected(std::move(r.error()));
            return Variant(std::in_place_index<I>, std::move(*r));
        }
        return decode_variant_alt<Variant, I + 1>(index, v, opt, path, depth);
    }
}

template <typename T> result<Value> encode(const T& x, const options& opt, std::string_view path, std::size_t depth) {
    using U = clean_t<T>;

    if (depth > opt.max_depth) {
        return fail<Value>(path, "maximum nesting depth exceeded");
    }

    if constexpr (std::same_as<U, Value>) {
        return x;
    } else if constexpr (std::same_as<U, Message>) {
        ObjectArray out;
        out.reserve(2);
        out.emplace_back(Value(x.encrypted));
        auto r = encode(static_cast<const MessageVariant&>(x), opt, child_path(path, 1), depth + 1);
        if (!r)
            return std::unexpected(std::move(r.error()));
        out.emplace_back(std::move(*r));
        return Value(std::move(out));
    } else if constexpr (std::same_as<U, std::nullptr_t> || std::same_as<U, std::monostate>) {
        return Value{};
    } else if constexpr (is_optional_v<U>) {
        if (!x)
            return Value{};
        return encode(*x, opt, path, depth + 1);
    } else if constexpr (std::same_as<U, std::byte>) {
        return Value(static_cast<uint8_t>(std::to_integer<uint8_t>(x)));
    } else if constexpr (std::is_enum_v<U>) {
        return encode(static_cast<std::underlying_type_t<U>>(x), opt, path, depth + 1);
    } else if constexpr (std::same_as<U, std::string>) {
        return Value(x);
    } else if constexpr (std::same_as<U, bool>) {
        return Value(x);
    } else if constexpr (std::integral<U>) {
        if constexpr (is_exact_native_v<U>)
            return Value(x);
        else if constexpr (sizeof(U) <= 2)
            return Value(static_cast<int32_t>(x));
        else
            return Value(static_cast<int64_t>(x));
    } else if constexpr (std::floating_point<U>) {
        if constexpr (is_exact_native_v<U>)
            return Value(x);
        else
            return Value(static_cast<double>(x));
    } else if constexpr (std::same_as<U, Hashtable>) {
        return Value(std::make_shared<Hashtable>(x));
    } else if constexpr (is_std_vector_v<U>) {
        return encode_range<typename U::value_type>(x.begin(), x.end(), x.size(), opt, path, depth);
    } else if constexpr (is_std_array_v<U>) {
        return encode_range<typename U::value_type>(x.begin(), x.end(), x.size(), opt, path, depth);
    } else if constexpr (is_std_pair_v<U>) {
        ObjectArray out;
        out.reserve(2);
        auto a = encode(x.first, opt, child_path(path, 0), depth + 1);
        if (!a)
            return std::unexpected(std::move(a.error()));
        auto b = encode(x.second, opt, child_path(path, 1), depth + 1);
        if (!b)
            return std::unexpected(std::move(b.error()));
        out.emplace_back(std::move(*a));
        out.emplace_back(std::move(*b));
        return Value(std::move(out));
    } else if constexpr (is_std_tuple_v<U>) {
        return encode_tuple_impl(x, opt, path, depth, std::make_index_sequence<std::tuple_size_v<U>>{});
    } else if constexpr (is_std_variant_v<U>) {
        if (x.valueless_by_exception()) {
            return fail<Value>(path, "variant is valueless_by_exception");
        }
        ObjectArray out;
        out.reserve(2);
        out.emplace_back(Value(static_cast<int32_t>(x.index())));

        error err{};
        bool ok = true;
        std::visit(
            [&](const auto& alt) {
                auto r = encode(alt, opt, child_path(path, 1), depth + 1);
                if (!r) {
                    err = std::move(r.error());
                    ok = false;
                    return;
                }
                out.emplace_back(std::move(*r));
            },
            x);

        if (!ok)
            return std::unexpected(std::move(err));
        return Value(std::move(out));
    } else if constexpr (is_map_like_v<U>) {
        ObjectArray out;
        out.reserve(x.size());

        std::size_t i = 0;
        for (auto const& [k, val] : x) {
            auto kr = encode(k, opt, child_path(child_path(path, i), 0), depth + 1);
            if (!kr)
                return std::unexpected(std::move(kr.error()));
            auto vr = encode(val, opt, child_path(child_path(path, i), 1), depth + 1);
            if (!vr)
                return std::unexpected(std::move(vr.error()));

            ObjectArray entry;
            entry.reserve(2);
            entry.emplace_back(std::move(*kr));
            entry.emplace_back(std::move(*vr));
            out.emplace_back(std::move(entry));
            ++i;
        }
        return Value(std::move(out));
    } else if constexpr (is_exact_native_v<U>) {
        return Value(x);
    } else if constexpr (pfr_reflectable<U>) {
        return encode_pfr_impl(x, opt, path, depth, std::make_index_sequence<boost::pfr::tuple_size_v<U>>{});
    } else {
        static_assert(always_false<U>::value, "luxon::ser::pfr_codec: unsupported type. Supported: aggregate structs "
                                              "(Boost.PFR), std::optional, std::vector, std::array, std::pair, std::tuple, "
                                              "std::variant, std::map/std::unordered_map, enums, arithmetic types, and "
                                              "native luxon::ser::Value-compatible types. Raw C arrays/references are not "
                                              "supported for deserialization.");
    }
}

template <typename T> result<T> decode(const Value& v, const options& opt, std::string_view path, std::size_t depth) {
    using U = clean_t<T>;

    if (depth > opt.max_depth) {
        return fail<U>(path, "maximum nesting depth exceeded");
    }

    if constexpr (std::same_as<U, Value>) {
        return v;
    } else if constexpr (std::same_as<U, Message>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "list[2]", v);
        if (list->size() != 2) {
            return fail<U>(path, "expected list of size 2 for Message");
        }
        auto enc = decode<bool>((*list)[0], opt, child_path(path, 0), depth + 1);
        if (!enc)
            return std::unexpected(std::move(enc.error()));
        auto mv = decode<MessageVariant>((*list)[1], opt, child_path(path, 1), depth + 1);
        if (!mv)
            return std::unexpected(std::move(mv.error()));
        return Message(std::move(*mv), *enc);
    } else if constexpr (std::same_as<U, std::nullptr_t>) {
        if (v.is_null())
            return nullptr;
        return type_error<U>(path, "null", v);
    } else if constexpr (std::same_as<U, std::monostate>) {
        if (v.is_null())
            return std::monostate{};
        return type_error<U>(path, "null", v);
    } else if constexpr (is_optional_v<U>) {
        using E = typename U::value_type;
        if (v.is_null())
            return U{};
        auto r = decode<E>(v, opt, path, depth + 1);
        if (!r)
            return std::unexpected(std::move(r.error()));
        return U{std::move(*r)};
    } else if constexpr (std::same_as<U, bool>) {
        if (auto p = v.get_ptr<bool>())
            return *p;
        return type_error<U>(path, "bool", v);
    } else if constexpr (std::same_as<U, std::string>) {
        if (auto p = v.get_ptr<std::string>())
            return *p;
        return type_error<U>(path, "string", v);
    } else if constexpr (std::same_as<U, std::byte>) {
        if (auto p = v.get_ptr<uint8_t>())
            return static_cast<std::byte>(*p);
        return type_error<U>(path, "uint8_t (for std::byte)", v);
    } else if constexpr (std::is_enum_v<U>) {
        using Under = std::underlying_type_t<U>;
        if (auto p = v.get_ptr<Under>())
            return static_cast<U>(*p);
        return type_error<U>(path, "exact underlying enum type", v);
    } else if constexpr (std::integral<U> || std::floating_point<U>) {
        if constexpr (is_exact_native_v<U>) {
            if (auto p = v.get_ptr<U>())
                return *p;
        } else {
            std::optional<U> extracted;
            std::visit(
                [&](const auto& arg) {
                    using A = std::decay_t<decltype(arg)>;
                    // Statically filter out variant's non-numeric payloads (strings, arrays, etc.)
                    if constexpr (std::integral<A> || std::floating_point<A>)
                        if constexpr (!std::same_as<A, bool>)
                            extracted = static_cast<U>(arg);
                },
                v.value);

            if (extracted)
                return *extracted;
        }
        return type_error<U>(path, "numeric type", v);
    } else if constexpr (is_std_vector_v<U>) {
        using E = typename U::value_type;

        if (auto exact = try_direct_vector<E>(v, path); exact) {
            return std::move(*exact);
        }

        if (auto list = as_list_ptr(v)) {
            U out;
            out.reserve(list->size());
            for (std::size_t i = 0; i < list->size(); ++i) {
                auto r = decode<E>((*list)[i], opt, child_path(path, i), depth + 1);
                if (!r)
                    return std::unexpected(std::move(r.error()));
                out.push_back(std::move(*r));
            }
            return out;
        }

        return type_error<U>(path, "vector/list", v);
    } else if constexpr (is_std_array_v<U>) {
        using E = typename U::value_type;
        constexpr std::size_t N = std::tuple_size_v<U>;

        auto vr = decode<std::vector<E>>(v, opt, path, depth + 1);
        if (!vr)
            return std::unexpected(std::move(vr.error()));
        if (vr->size() != N) {
            return fail<U>(path, "expected array of size " + std::to_string(N) + ", got " + std::to_string(vr->size()));
        }
        auto tmp = std::move(*vr);
        return vector_to_array<U>(tmp, std::make_index_sequence<N>{});
    } else if constexpr (is_std_pair_v<U>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "list[2]", v);
        if (list->size() != 2)
            return fail<U>(path, "expected pair encoded as list of size 2");

        using A = typename U::first_type;
        using B = typename U::second_type;

        auto a = decode<A>((*list)[0], opt, child_path(path, 0), depth + 1);
        if (!a)
            return std::unexpected(std::move(a.error()));
        auto b = decode<B>((*list)[1], opt, child_path(path, 1), depth + 1);
        if (!b)
            return std::unexpected(std::move(b.error()));

        return U{std::move(*a), std::move(*b)};
    } else if constexpr (is_std_tuple_v<U>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "list/tuple", v);
        constexpr std::size_t N = std::tuple_size_v<U>;
        if (list->size() != N) {
            return fail<U>(path, "expected tuple of size " + std::to_string(N) + ", got " + std::to_string(list->size()));
        }
        return decode_tuple_impl<U>(*list, opt, path, depth, std::make_index_sequence<N>{});
    } else if constexpr (is_std_variant_v<U>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "variant list[2]", v);
        if (list->size() != 2)
            return fail<U>(path, "expected variant encoded as list of size 2");

        auto idx = decode<std::size_t>((*list)[0], opt, child_path(path, 0), depth + 1);
        if (!idx)
            return std::unexpected(std::move(idx.error()));

        return decode_variant_alt<U>(*idx, (*list)[1], opt, child_path(path, 1), depth + 1);
    } else if constexpr (is_map_like_v<U>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "map/list", v);

        U out;
        if constexpr (requires(U& m) { m.reserve(list->size()); }) {
            out.reserve(list->size());
        }

        for (std::size_t i = 0; i < list->size(); ++i) {
            auto entry = as_list_ptr((*list)[i]);
            if (!entry || entry->size() != 2) {
                return fail<U>(child_path(path, i), "expected map entry as list of size 2");
            }

            using K = typename U::key_type;
            using M = typename U::mapped_type;

            auto k = decode<K>((*entry)[0], opt, child_path(child_path(path, i), 0), depth + 1);
            if (!k)
                return std::unexpected(std::move(k.error()));
            auto m = decode<M>((*entry)[1], opt, child_path(child_path(path, i), 1), depth + 1);
            if (!m)
                return std::unexpected(std::move(m.error()));

            out.insert_or_assign(std::move(*k), std::move(*m));
        }

        return out;
    } else if constexpr (is_exact_native_v<U>) {
        if (auto p = v.get_ptr<U>())
            return *p;
        return fail<U>(path, "type mismatch; got " + std::string(value_kind(v)));
    } else if constexpr (pfr_reflectable<U>) {
        auto list = as_list_ptr(v);
        if (!list)
            return type_error<U>(path, "struct/list", v);

        constexpr std::size_t N = boost::pfr::tuple_size_v<U>;
        if (list->size() != N) {
            return fail<U>(path, "expected struct field-count " + std::to_string(N) + ", got " + std::to_string(list->size()));
        }

        return decode_pfr_impl<U>(*list, opt, path, depth, std::make_index_sequence<N>{});
    } else {
        static_assert(always_false<U>::value, "luxon::ser::pfr_codec: unsupported type. Supported: aggregate structs "
                                              "(Boost.PFR), std::optional, std::vector, std::array, std::pair, std::tuple, "
                                              "std::variant, std::map/std::unordered_map, enums, arithmetic types, and "
                                              "native luxon::ser::Value-compatible types. Raw C arrays/references are not "
                                              "supported for deserialization.");
    }
}

} // namespace detail

template <typename T> result<Value> to_value(const T& x, const options& opt) { return detail::encode(x, opt, "$", 0); }

template <typename T> result<T> from_value(const Value& v, const options& opt) { return detail::decode<T>(v, opt, "$", 0); }

} // namespace server::pfr_codec
