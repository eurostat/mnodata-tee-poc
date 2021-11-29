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

#include "Entities.h"
#include "StreamAdditions.h"
#include <sharemind-hi/enclave/common/File.h>

namespace eurostat {
namespace enclave {
namespace full_analysis {

using HFileSource = PersistentDataSource<PseudonymisedUserFootprintUpdates, sharemind_hi::enclave::File>;
using SFileSource = PersistentDataSource<AccumulatedUserFootprint, SgxEncryptedFile>;
using SFileSink = PersistentDataSinkBuilder;

enum class Perform { OnlyStateUpdate, FullAnalysis };

void run(HFileSource h_file,
         SFileSource s_file_in,
         SFileSink s_file_out,
         PseudonymisationKeyRef pseudonymisation_key,
         Perform what_to_do,
         ReferenceAreas const & reference_areas,
         CensusResidents const & residents,
         bool with_calibration,
         sharemind_hi::enclave::TaskOutputs & outputs,
         Log & application_log);

} // namespace full_analysis
} // namespace enclave
} // namespace eurostat
