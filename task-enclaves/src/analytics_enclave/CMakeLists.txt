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
PROJECT("EUROSTAT_ANALYTICS_ENCLAVE" "C" "CXX")
INCLUDE(HIPolicies NO_POLICY_SCOPE)

INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/config.local" OPTIONAL)
INCLUDE("${CMAKE_CURRENT_BINARY_DIR}/config.local" OPTIONAL)

# Using COMPONENTS you can specify which mitigatons to use for each SGX library.
# By default, the full LVI mitigation is used. Look into the cmake/FindSgxSdk.cmake
# for more information. This needs to be loaded before the package "sharemind-hi"
# as otherwise the default targets are used.
FIND_PACKAGE(SgxSdk
    REQUIRED COMPONENTS
        trts_lvi_none
        tservice_lvi_none
        tcxx_lvi_none
        tstdc_lvi_none
        tprotected_fs_lvi_none
        tkey_exchange_lvi_none
        tcrypto_lvi_none
)
FIND_PACKAGE(sharemind-hi REQUIRED COMPONENTS task-trusted)

ADD_LIBRARY(analytics_enclave MODULE
    "Comparison.h"
    "Enclave.cpp"
    "Entities.h"
    "FullAnalysis.cpp"
    "FullAnalysis.h"
    "HiInternalApiDuplication.h"
    "Parameters.h"
    "Pseudonymisation.cpp"
    "Pseudonymisation.h"
    "Seal.cpp"
    "Seal.h"
    "SgxEncryptedFile.cpp"
    "SgxEncryptedFile.h"
    "StreamAdditions.h"
    "Xoroshiro.h"
)

TARGET_COMPILE_OPTIONS(analytics_enclave
    PRIVATE "-Wall" "-Wextra"
)

IF("${SGX_MODE}" STREQUAL "HW")
    # Use more memory in  mode, just to be sure no funny
    # OOM crashes happen during presentations. The pipeline buffers
    # of the Streams API should not grow, but the hash maps, although
    # said to not grow over a given maximum, are not bound by code to
    # a certain size. So instead, we just give it a lot more memory.
    # This comes with the cost of larger enclave startup times, but
    # this is still neglectable compared to the expected runtime.
    SET(HEAP_MAX_SIZE 0xC0000000) # 3 GiB
ELSE()
	# In simulation modes (the primary development mode) the heap is
	# kept small, so enclave startup times in `taskRun` are small.
    SET(HEAP_MAX_SIZE 0x20000000) # 512 MiB
ENDIF()

HIProcessEnclaveTarget(analytics_enclave
    PROJECT_NAME "${SHAREMINDHI_PROJECT_NAME}"
    STACK_MAX_SIZE 0x4000000
    HEAP_MAX_SIZE ${HEAP_MAX_SIZE}
    PROD_ID "${analytics_enclave_PROD_ID}"
    ISV_SVN "${analytics_enclave_ISV_SVN}"
    DISPLAY_NAME "${SHAREMINDHI_PROJECT_DISPLAY} Analytics Enclave"
    PRIVATE_KEY "${ENCLAVE_SIGNING_PRIVATE_KEY}"
    # This parameter only influences the code compiled in this project.
    # Options: load, cf, none. See Intel LVI Deep Dive for information.
    LVI_MITIGATION none
    LINK_LIBRARIES
        sharemind-hi::task_trusted_stream
        sharemind-hi::task_trusted
)
