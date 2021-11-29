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
PROJECT("EUROSTAT_PSEUDONYMISATION_KEY_ENCLAVE" "C" "CXX")
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
FIND_PACKAGE(sharemind-hi REQUIRED COMPONENTS task-trusted)

ADD_LIBRARY(pseudonymisation_key_enclave MODULE
    "Enclave.cpp"
    "Entities.h"
)

TARGET_COMPILE_OPTIONS(pseudonymisation_key_enclave
    PRIVATE "-Wall" "-Wextra"
)

HIProcessEnclaveTarget(pseudonymisation_key_enclave
    PROJECT_NAME "${SHAREMINDHI_PROJECT_NAME}"
    STACK_MAX_SIZE 0x4000000
    HEAP_MAX_SIZE 0x8000000
    PROD_ID "${pseudonymisation_key_enclave_PROD_ID}"
    ISV_SVN "${pseudonymisation_key_enclave_ISV_SVN}"
    DISPLAY_NAME "${SHAREMINDHI_PROJECT_DISPLAY} Pseudonymisation Key Enclave"
    PRIVATE_KEY "${ENCLAVE_SIGNING_PRIVATE_KEY}"
    # This parameter only influences the code compiled in this project.
    # Options: load, cf, none. See Intel LVI Deep Dive for information.
    LVI_MITIGATION cf
    LINK_LIBRARIES
        sharemind-hi::task_trusted_stream
        sharemind-hi::task_trusted
)
