/*
 * Copyright (C) 2026 Comcast
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FireboltObject.h"

#if ENABLE(FIREBOLT)

#include <cmath>
#include <wtf/JSONValues.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

namespace firebolt {

namespace {

String toWTF(const std::string& s) { return String::fromUTF8(s.c_str()); }

// WTF::JSON values are shared and mutable; FireboltObject has value semantics.
Ref<JSON::Value> clone(const JSON::Value& v)
{
    switch (v.type()) {
    case JSON::Value::Type::Null:
        return JSON::Value::null();
    case JSON::Value::Type::Boolean:
        return JSON::Value::create(*v.asBoolean());
    case JSON::Value::Type::Integer:
        return JSON::Value::create(*v.asInteger());
    case JSON::Value::Type::Double:
        return JSON::Value::create(*v.asDouble());
    case JSON::Value::Type::String:
        return JSON::Value::create(v.asString());
    case JSON::Value::Type::Object: {
        auto result = JSON::Object::create();
        auto source = v.asObject();
        for (auto& entry : *source)
            result->setValue(entry.key, clone(entry.value.get()));
        return result;
    }
    case JSON::Value::Type::Array: {
        auto result = JSON::Array::create();
        auto source = const_cast<JSON::Value&>(v).asArray();
        for (size_t i = 0; i < source->length(); ++i)
            result->pushValue(clone(source->get(i).get()));
        return result;
    }
    }
    return JSON::Value::null();
}

} // namespace

struct FireboltObject::Impl {
    Ref<JSON::Value> json { JSON::Object::create() };

    Impl() = default;
    Impl(const Impl& other) : json(clone(other.json.get())) { }

    RefPtr<JSON::Object> ensureObject()
    {
        if (json->type() != JSON::Value::Type::Object)
            json = JSON::Object::create();
        return json->asObject();
    }
};

FireboltObject::FireboltObject() : impl_(new Impl) { }
FireboltObject::FireboltObject(const FireboltObject& o) : impl_(new Impl(*o.impl_)) { }
FireboltObject& FireboltObject::operator=(const FireboltObject& o)
{
    if (this != &o)
        impl_->json = clone(o.impl_->json.get());
    return *this;
}
FireboltObject::~FireboltObject() = default;

FireboltObject::Kind FireboltObject::GetKind() const
{
    switch (impl_->json->type()) {
    case JSON::Value::Type::Null:    return Kind::Null;
    case JSON::Value::Type::Boolean: return Kind::Bool;
    case JSON::Value::Type::Double:
    case JSON::Value::Type::Integer: return Kind::Number;
    case JSON::Value::Type::String:  return Kind::String;
    case JSON::Value::Type::Array:   return Kind::Array;
    case JSON::Value::Type::Object:  return Kind::Object;
    }
    return Kind::Null;
}

void FireboltObject::SetNull() { impl_->json = JSON::Value::null(); }
void FireboltObject::SetBool(bool v) { impl_->json = JSON::Value::create(v); }
void FireboltObject::SetNumber(double v)
{
    if (std::trunc(v) == v && std::abs(v) < 2147483647.0)
        impl_->json = JSON::Value::create(static_cast<int>(v));
    else
        impl_->json = JSON::Value::create(v);
}
void FireboltObject::SetString(const std::string& v) { impl_->json = JSON::Value::create(toWTF(v)); }

bool FireboltObject::GetBool(bool& out) const
{
    auto v = impl_->json->asBoolean();
    if (!v)
        return false;
    out = *v;
    return true;
}

bool FireboltObject::GetNumber(double& out) const
{
    auto v = impl_->json->asDouble();
    if (!v)
        return false;
    out = *v;
    return true;
}

bool FireboltObject::GetString(std::string& out) const
{
    if (impl_->json->type() != JSON::Value::Type::String)
        return false;
    auto utf8 = impl_->json->asString().utf8();
    out.assign(utf8.data(), utf8.length());
    return true;
}

bool FireboltObject::Has(const std::string& name) const
{
    auto object = impl_->json->asObject();
    return object && object->getValue(toWTF(name));
}

void FireboltObject::SetChild(const std::string& name, const FireboltObject& child)
{
    impl_->ensureObject()->setValue(toWTF(name), clone(child.impl_->json.get()));
}

bool FireboltObject::GetChild(const std::string& name, FireboltObject& out) const
{
    auto object = impl_->json->asObject();
    if (!object)
        return false;
    auto value = object->getValue(toWTF(name));
    if (!value)
        return false;
    out.impl_->json = clone(*value);
    return true;
}

void FireboltObject::AppendChild(const FireboltObject& child)
{
    if (impl_->json->type() != JSON::Value::Type::Array)
        impl_->json = JSON::Array::create();
    impl_->json->asArray()->pushValue(clone(child.impl_->json.get()));
}

size_t FireboltObject::Size() const
{
    if (auto array = impl_->json->asArray())
        return array->length();
    if (auto object = impl_->json->asObject())
        return object->size();
    return 0;
}

bool FireboltObject::GetAt(size_t index, FireboltObject& out) const
{
    auto array = impl_->json->asArray();
    if (!array || index >= array->length())
        return false;
    out.impl_->json = clone(array->get(index).get());
    return true;
}

std::string FireboltObject::ToString() const
{
    auto utf8 = impl_->json->toJSONString().utf8();
    return std::string(utf8.data(), utf8.length());
}

bool FireboltObject::FromString(const std::string& payload)
{
    auto parsed = JSON::Value::parseJSON(toWTF(payload));
    if (!parsed)
        return false;
    impl_->json = parsed.releaseNonNull();
    return true;
}

} // namespace firebolt

#endif // ENABLE(FIREBOLT)
