// Copyright (c) Facebook, Inc. and its affiliates.

// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "ABI35_0_0JSIndexedRAMBundle.h"

#include <folly/Memory.h>

namespace facebook {
namespace ReactABI35_0_0 {

std::function<std::unique_ptr<JSModulesUnbundle>(std::string)> JSIndexedRAMBundle::buildFactory() {
  return [](const std::string& bundlePath){
    return folly::make_unique<JSIndexedRAMBundle>(bundlePath.c_str());
  };
}

JSIndexedRAMBundle::JSIndexedRAMBundle(const char *sourcePath) :
    m_bundle (sourcePath, std::ios_base::in) {
  if (!m_bundle) {
    throw std::ios_base::failure(
      folly::to<std::string>("Bundle ", sourcePath,
                             "cannot be opened: ", m_bundle.rdstate()));
  }

  // read in magic header, number of entries, and length of the startup section
  uint32_t header[3];
  static_assert(
    sizeof(header) == 12,
    "header size must exactly match the input file format");

  readBundle(reinterpret_cast<char *>(header), sizeof(header));
  const size_t numTableEntries = folly::Endian::little(header[1]);
  const size_t startupCodeSize = folly::Endian::little(header[2]);

  // allocate memory for meta data and lookup table.
  m_table = ModuleTable(numTableEntries);
  m_baseOffset = sizeof(header) + m_table.byteLength();

  // read the lookup table from the file
  readBundle(
    reinterpret_cast<char *>(m_table.data.get()), m_table.byteLength());

  // read the startup code
  m_startupCode = std::unique_ptr<JSBigBufferString>(new JSBigBufferString{startupCodeSize - 1});

  readBundle(m_startupCode->data(), startupCodeSize - 1);
}

JSIndexedRAMBundle::Module JSIndexedRAMBundle::getModule(uint32_t moduleId) const {
  Module ret;
  ret.name = folly::to<std::string>(moduleId, ".js");
  ret.code = getModuleCode(moduleId);
  return ret;
}

std::unique_ptr<const JSBigString> JSIndexedRAMBundle::getStartupCode() {
  CHECK(m_startupCode) << "startup code for a RAM Bundle can only be retrieved once";
  return std::move(m_startupCode);
}

std::string JSIndexedRAMBundle::getModuleCode(const uint32_t id) const {
  const auto moduleData = id < m_table.numEntries ? &m_table.data[id] : nullptr;

  // entries without associated code have offset = 0 and length = 0
  const uint32_t length = moduleData ? folly::Endian::little(moduleData->length) : 0;
  if (length == 0) {
    throw std::ios_base::failure(
      folly::to<std::string>("Error loading module", id, "from RAM Bundle"));
  }

  std::string ret(length - 1, '\0');
  readBundle(&ret.front(), length - 1, m_baseOffset + folly::Endian::little(moduleData->offset));
  return ret;
}

void JSIndexedRAMBundle::readBundle(char *buffer, const std::streamsize bytes) const {
  if (!m_bundle.read(buffer, bytes)) {
    if (m_bundle.rdstate() & std::ios::eofbit) {
      throw std::ios_base::failure("Unexpected end of RAM Bundle file");
    }
    throw std::ios_base::failure(
      folly::to<std::string>("Error reading RAM Bundle: ", m_bundle.rdstate()));
  }
}

void JSIndexedRAMBundle::readBundle(
    char *buffer,
    const std::streamsize bytes,
    const std::ifstream::pos_type position) const {

  if (!m_bundle.seekg(position)) {
    throw std::ios_base::failure(
      folly::to<std::string>("Error reading RAM Bundle: ", m_bundle.rdstate()));
  }
  readBundle(buffer, bytes);
}

}  // namespace ReactABI35_0_0
}  // namespace facebook
