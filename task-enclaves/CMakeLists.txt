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
PROJECT("EUROSTAT" "C" "CXX")

INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/config.local" OPTIONAL)
INCLUDE("${CMAKE_CURRENT_BINARY_DIR}/config.local" OPTIONAL)

SET(HI_CMAKE_INCLUDE_DIR "${sharemind-hi_ROOT}/lib/cmake/sharemind-hi")
SET(HI_LOCAL_CMAKE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
SET(CMAKE_MODULE_PATH
    "${HI_LOCAL_CMAKE_INCLUDE_DIR}/"
    "${HI_CMAKE_INCLUDE_DIR}/"
    ${CMAKE_MODULE_PATH}
)

IF(CMAKE_VERSION VERSION_LESS 3.12)
    SET(sharemind-hi_DIR "${HI_CMAKE_INCLUDE_DIR}")
ENDIF()

SET(SHAREMINDHI_PROJECT_NAME "eurostat")
SET(SHAREMINDHI_PROJECT_DISPLAY "Eurostat")

SET(CPACK_PACKAGE_VERSION_MAJOR "1")
SET(CPACK_PACKAGE_VERSION_MINOR "0")
SET(CPACK_PACKAGE_VERSION_PATCH "2")
SET(CPACK_PACKAGE_NAME "${SHAREMINDHI_PROJECT_NAME}")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${SHAREMINDHI_PROJECT_DISPLAY} task enclave")
SET(CPACK_PACKAGE_DESCRIPTION "${SHAREMINDHI_PROJECT_DISPLAY} task enclave")

INCLUDE(HIPackaging REQUIRED)
INCLUDE(HISetup REQUIRED)
HI_SETUP_CMAKE_INCLUDES("${HI_LOCAL_CMAKE_INCLUDE_DIR}")
INCLUDE(HIVersionToIsvSvn REQUIRED)
HI_VERSION_TO_ISV_SVN("${CPACK_PACKAGE_VERSION_MAJOR}" "${CPACK_PACKAGE_VERSION_MINOR}" "${CPACK_PACKAGE_VERSION_PATCH}" COMMON_ISV_SVN)

# Refers to the PROD_ID in the enclave configuration .xml file.
SET(analytics_enclave_PROD_ID 1004)
SET(pseudonymisation_key_enclave_PROD_ID 1005)

# When you update your enclave, you should increase the ISV_SVN.
# Refers to the ISV_SVN in the enclave configuration .xml file.
SET(analytics_enclave_ISV_SVN ${COMMON_ISV_SVN})
SET(pseudonymisation_key_enclave_ISV_SVN ${COMMON_ISV_SVN})

SET_PROPERTY(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS TRUE)

ADD_SUBDIRECTORY("src/analytics_enclave/")
ADD_SUBDIRECTORY("src/pseudonymisation_key_enclave/")

ENABLE_TESTING()

ADD_TEST(
    NAME integration__performance_test
    COMMAND
        "${CMAKE_COMMAND}" -E env
            "BUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
            "TEST_TYPE=performance"
            "${CMAKE_SOURCE_DIR}/test/client-and-server.sh"
)
ADD_TEST(
    NAME integration__business_cycle_test
    COMMAND
        "${CMAKE_COMMAND}" -E env
            "BUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
            "TEST_TYPE=businesscycle"
            "${CMAKE_SOURCE_DIR}/test/client-and-server.sh"
)
ADD_SUBDIRECTORY("unit-test/")
