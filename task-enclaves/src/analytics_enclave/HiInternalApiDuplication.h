/*
* Copyright 2021 European Union
*
* Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
* the European Commission - subsequent versions of the EUPL (the "Licence");
* You may not use this work except in compliance with the Licence.
* You may obtain a copy of the Licence at:
*
* https://joinup.ec.europa.eu/software/page/eupl
*
* Unless required by applicable law or agreed to in writing, software 
* distributed under the Licence is distributed on an "AS IS" basis,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the Licence for the specific language governing permissions and 
* limitations under the Licence.
*/ 

#pragma once

#include <functional>
#include <sgx_error.h>
#include <sharemind-hi/common/UntrustedFileSystemId.h>
#include <sharemind-hi/enclave/common/File.h>

extern "C" uint64_t enclave_untrusted_steady_clock_millis();
extern "C" sgx_status_t SGX_CDECL
get_data_file_path_ocall(sgx_status_t * retval,
                         const UntrustedFileSystemId * id,
                         char * buffer,
                         size_t buffer_size);
