// CRTP model contract + primitives. Header-only; no virtuals.
//
// A generated model T provides:
//     void Write(FireboltObject&) const;
//     bool Read(const FireboltObject&);      // false => payload does not fit the model
// and inherits ToObject/FromObject/ToString/FromString from FireboltModel<T>.


#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "FireboltObject.h"

namespace firebolt {

template<typename T>
class FireboltModel {
public:
    FireboltObject ToObject() const
    {
        FireboltObject object;
        static_cast<const T*>(this)->Write(object);
        return object;
    }
    bool FromObject(const FireboltObject& object) { return static_cast<T*>(this)->Read(object); }

    std::string ToString() const { return ToObject().ToString(); }
    bool FromString(const std::string& payload)
    {
        FireboltObject object;
        return object.FromString(payload) && FromObject(object);
    }

protected:
    FireboltModel() = default;
    ~FireboltModel() = default;
};

namespace detail {
template<typename V> void WriteScalar(FireboltObject& o, const V& v)
{
    if constexpr (std::is_same_v<V, bool>)
        o.SetBool(v);
    else if constexpr (std::is_arithmetic_v<V>)
        o.SetNumber(static_cast<double>(v));
    else
        o.SetString(v);
}
template<typename V> bool ReadScalar(const FireboltObject& o, V& v)
{
    if constexpr (std::is_same_v<V, bool>)
        return o.GetBool(v);
    else if constexpr (std::is_arithmetic_v<V>) {
        double d;
        if (!o.GetNumber(d)) return false;
        v = static_cast<V>(d);
        return true;
    } else
        return o.GetString(v);
}
} // namespace detail

struct NullType { };

template<typename T>
class FireboltPrimitive : public FireboltModel<FireboltPrimitive<T>> {
public:
    T value {};
    FireboltPrimitive() = default;
    FireboltPrimitive(T v) : value(std::move(v)) { }

    void Write(FireboltObject& o) const { detail::WriteScalar(o, value); }
    bool Read(const FireboltObject& o) { return detail::ReadScalar(o, value); }
};

template<>
class FireboltPrimitive<NullType> : public FireboltModel<FireboltPrimitive<NullType>> {
public:
    void Write(FireboltObject& o) const { o.SetNull(); }
    bool Read(const FireboltObject&) { return true; }
};

using FireboltString          = FireboltPrimitive<std::string>;
using FireboltBoolean         = FireboltPrimitive<bool>;
using FireboltInteger         = FireboltPrimitive<int32_t>;
using FireboltUnsignedInteger = FireboltPrimitive<uint32_t>;
using FireboltFloat           = FireboltPrimitive<double>;
using FireboltVoid            = FireboltPrimitive<NullType>;

} // namespace firebolt
