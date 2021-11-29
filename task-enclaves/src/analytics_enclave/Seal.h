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

#include <cstddef>
#include <functional>
#include <sharemind-hi/enclave/common/File.h>

namespace eurostat {
namespace enclave {

void sealData(sharemind_hi::enclave::File & outFile,
              void const * const data,
              std::size_t const dataSize,
              void const * const aad,
              std::size_t const aadSize);
void unsealData(sharemind_hi::enclave::File & inFile,
                void const * const expectedAad,
                std::size_t const expectedAadSize,
                std::function<void *(std::size_t)> const allocUnsealedData);

}
}
