// Copyright 2026 Summon Software Labs
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Proof obligations for the version surface: the version string is composed
// from the three components, the banner names the product, the version and the
// program position, and every format version the public contract fixes is 1.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::kCanonicalFormatVersion;
using cooling_capacity_accounting::kCanonicalMagic;
using cooling_capacity_accounting::kInterchangeFormatVersion;
using cooling_capacity_accounting::kProductName;
using cooling_capacity_accounting::kProgramPosition;
using cooling_capacity_accounting::kRepositorySlug;
using cooling_capacity_accounting::kStoreFormatVersion;
using cooling_capacity_accounting::kVersionMajor;
using cooling_capacity_accounting::kVersionMinor;
using cooling_capacity_accounting::kVersionPatch;
using cooling_capacity_accounting::version_banner;
using cooling_capacity_accounting::version_string;

}  // namespace

CCA_TEST(version_string_is_built_from_its_components) {
  CCA_CHECK_EQ(kVersionMajor, 1);
  CCA_CHECK_EQ(kVersionMinor, 0);
  CCA_CHECK_EQ(kVersionPatch, 0);

  const std::string version = version_string();
  CCA_CHECK_EQ(version, std::string("1.0.0"));
  CCA_CHECK_EQ(version, std::to_string(kVersionMajor) + "." +
                            std::to_string(kVersionMinor) + "." +
                            std::to_string(kVersionPatch));

  // Exactly two separators, at the documented positions, with no leading or
  // trailing separator and no empty component.
  CCA_CHECK_EQ(version.size(), 5U);
  CCA_CHECK_EQ(version.find('.'), 1U);
  CCA_CHECK_EQ(version.rfind('.'), 3U);
  CCA_CHECK_EQ(version.find_first_not_of("0123456789."), std::string::npos);
  CCA_CHECK_EQ(version.front(), '1');
  CCA_CHECK_EQ(version.back(), '0');

  // The function is stable across calls.
  CCA_CHECK_EQ(version_string(), version);
}

CCA_TEST(version_banner_names_product_version_and_position) {
  const std::string banner = version_banner();
  CCA_CHECK(!banner.empty());
  CCA_CHECK(banner.find(std::string(kProductName)) != std::string::npos);
  CCA_CHECK(banner.find(version_string()) != std::string::npos);
  CCA_CHECK(banner.find(std::string(kProgramPosition)) != std::string::npos);
  CCA_CHECK_EQ(banner.rfind(std::string(kProductName), 0U), 0U);
  CCA_CHECK_EQ(banner, std::string(kProductName) + " " + version_string() + " - " +
                           std::string(kProgramPosition));
  CCA_CHECK_EQ(version_banner(), banner);
}

CCA_TEST(product_identity_and_format_versions_are_fixed) {
  CCA_CHECK_EQ(kProductName, std::string_view("Cooling Capacity Accounting"));
  CCA_CHECK_EQ(kRepositorySlug, std::string_view("Cooling-Capacity-Accounting"));
  CCA_CHECK_EQ(kCanonicalFormatVersion, 1U);
  CCA_CHECK_EQ(kStoreFormatVersion, 1U);
  CCA_CHECK_EQ(kInterchangeFormatVersion, 1U);
  CCA_CHECK_EQ(kCanonicalMagic, std::string_view("CCAIN001"));
}

CCA_TEST(canonical_header_states_the_format_version) {
  const cca_test::FacilitySpec spec;
  const std::vector<std::uint8_t> encoded = [&spec]() {
    cca_test::FacilitySpec facility = spec;
    facility.zones = 1;
    facility.loops_per_zone = 1;
    facility.units_per_loop = 1;
    auto bytes = cooling_capacity_accounting::encode_canonical(
        cca_test::make_facility(facility));
    return bytes.ok() ? bytes.take() : std::vector<std::uint8_t>{};
  }();
  CCA_CHECK(encoded.size() > 24U);

  // Magic, format version and reserved field are the first sixteen bytes.
  const std::string_view magic(reinterpret_cast<const char*>(encoded.data()),
                               kCanonicalMagic.size());
  CCA_CHECK_EQ(magic, kCanonicalMagic);
  CCA_CHECK_EQ(encoded[8], 0U);
  CCA_CHECK_EQ(encoded[9], 0U);
  CCA_CHECK_EQ(encoded[10], 0U);
  CCA_CHECK_EQ(encoded[11], static_cast<std::uint8_t>(kCanonicalFormatVersion));
  CCA_CHECK_EQ(encoded[12], 0U);
  CCA_CHECK_EQ(encoded[13], 0U);
  CCA_CHECK_EQ(encoded[14], 0U);
  CCA_CHECK_EQ(encoded[15], 0U);
}
