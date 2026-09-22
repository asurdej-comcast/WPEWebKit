// FireboltObject: the generic runtime value every model serialises into /
// deserialises from. It hides the JSON representation (pimpl) so that
// generated headers never depend on a JSON library.

#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace firebolt {

class FireboltObject {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    FireboltObject();                                   // empty object: {}
    FireboltObject(const FireboltObject&);
    FireboltObject& operator=(const FireboltObject&);
    ~FireboltObject();

    Kind GetKind() const;

    // --- scalar values -------------------------------------------------
    void SetNull();
    void SetBool(bool v);
    void SetNumber(double v);
    void SetString(const std::string& v);
    bool GetBool(bool& out) const;                      // false on kind mismatch
    bool GetNumber(double& out) const;
    bool GetString(std::string& out) const;

    // --- object members ------------------------------------------------
    bool Has(const std::string& name) const;
    void SetChild(const std::string& name, const FireboltObject& child);
    bool GetChild(const std::string& name, FireboltObject& out) const;

    // --- array elements (AppendChild turns a non-array into an array) ----
    void AppendChild(const FireboltObject& child);
    size_t Size() const;
    bool GetAt(size_t index, FireboltObject& out) const;

    // --- typed helpers for generated models (anything with ToObject()/FromObject())
    template<typename Model>
    void Set(const std::string& name, const Model& model) { SetChild(name, model.ToObject()); }

    template<typename Model>
    bool Get(const std::string& name, Model& model) const
    {
        FireboltObject child;
        return GetChild(name, child) && model.FromObject(child);
    }

    // --- (de)serialisation ---------------------------------------------
    std::string ToString() const;
    bool FromString(const std::string& payload);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace firebolt
