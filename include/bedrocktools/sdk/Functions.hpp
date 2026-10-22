#pragma once

#include <bedrocktools/Api.hpp>
#include <bedrocktools/memory/Signatures.hpp>

namespace bedrocktools::sdk {

template <class Function>
Function function(memory::SignatureId id, const api::ApiV1* runtime = nullptr) {
    if (!runtime) runtime = api::find();
    const auto address = api::resolve(id, runtime);
    return address ? reinterpret_cast<Function>(address) : nullptr;
}

}
