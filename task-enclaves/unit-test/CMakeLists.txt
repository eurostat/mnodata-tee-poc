#
# Copyright 2021 European Union
#
# Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
# the European Commission - subsequent versions of the EUPL (the "Licence");
# You may not use this work except in compliance with the Licence.
# You may obtain a copy of the Licence at:
#
# https://joinup.ec.europa.eu/software/page/eupl
#
# Unless required by applicable law or agreed to in writing, software 
# distributed under the Licence is distributed on an "AS IS" basis,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the Licence for the specific language governing permissions and 
# limitations under the Licence.
#

CMAKE_MINIMUM_REQUIRED(VERSION "3.10.2")
PROJECT("EUROSTAT_ANALYTICS_ENCLAVE_UNIT_TEST" "C" "CXX")
INCLUDE(HIPolicies NO_POLICY_SCOPE)

INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/config.local" OPTIONAL)
INCLUDE("${CMAKE_CURRENT_BINARY_DIR}/config.local" OPTIONAL)

# Using COMPONENTS you can specify which mitigatons to use for each SGX library.
# By default, the full LVI mitigation is used. Look into the cmake/FindSgxSdk.cmake
# for more information. This needs to be loaded before the package "sharemind-hi"
# as otherwise the default targets are used.
FIND_PACKAGE(SgxSdk
    REQUIRED COMPONENTS
        #trts_lvi_cf
        #tservice_lvi_cf
        #tcxx_lvi_cf
        #tstdc_lvi_cf
        #tprotected_fs_lvi_cf
        #tkey_exchange_lvi_cf
        #tcrypto_lvi_load
)
FIND_PACKAGE(sharemind-hi REQUIRED COMPONENTS test-wrapper)

ADD_LIBRARY(unit-test MODULE
    "UnitTest.cpp"
    "../src/analytics_enclave/Pseudonymisation.cpp"
)

TARGET_COMPILE_OPTIONS(analytics_enclave
    PRIVATE "-Wall" "-Wextra"
)

HIAddEnclaveTest(
    NAME
        unit_test
    TARGET
        unit-test
    LINK_LIBRARIES_WHOLE_ARCHIVE
        sgxsdk::sgx_trts
        #LINK_LIBRARIES_NO_WHOLE_ARCHIVE
        #sharemind-hi::
)
