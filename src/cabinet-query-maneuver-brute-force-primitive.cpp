/*
 * Copyright (C) 2022  Lukas Birkemeyer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "cluon-complete.hpp"
#include "cabinet-query-maneuver-brute-force-primitive.hpp"

#include <iostream>
#include <sstream>
#include <string>

int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if ( (0 == commandlineArguments.count("cab")) ) {
    std::cerr << argv[0] << " query a cabinet (an lmdb-based key/value-database with accelerations in Morton format)." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cab=myStore.cab [--mem=32024] --box=bottom-left-accelX,bottom-left-accelY,top-right-accelX,top-right-accelY" << std::endl;
    std::cerr << "         --cab:         name of the cabinet file" << std::endl;
    std::cerr << "         --db:          name of the database to be used inside the cabinet file; default: 1030/2" << std::endl;
    std::cerr << "         --mem:         upper memory size for database in memory in GB, default: 64,000 (representing 64TB)" << std::endl;
    std::cerr << "         --box:         return all timeStamps within this rectangle specified by bottom-left and top-right X/Y accelerations; default: 0,-2,2,2" << std::endl;
    std::cerr << "         --start:       only include matching timeStamps that are larger than or equal to this timepoint in nanoseconds; default: 0" << std::endl;
    std::cerr << "         --end:         only include matching timeStamps that are less than this timepoint in nanoseconds; default: MAX" << std::endl;
    std::cerr << "         --printAll:    prints all timestamp candidates" << std::endl;
    std::cerr << "         --print:       prints only timestamps matching duration criteria" << std::endl;
    std::cerr << "         --minDiffTime: minimal difference between two consecutive timestamps in ms; default: 50" << std::endl;
    std::cerr << "         --minDuration: minimum duration of a maneuver in ms; default: 200" << std::endl;
    std::cerr << "         --maxDuration: maximum duration of a maneuver in ms; default: 3000" << std::endl;
    std::cerr << "         --verbose" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cab=myStore.cab --box=0,-2,2,2   # random driving" << std::endl;
    std::cerr << "         " << argv[0] << " --cab=myStore.cab --box=4,-4,10,4  # harsh braking" << std::endl;
    retCode = 1;
  } else {    
    const std::string CABINET{commandlineArguments["cab"]};
    const std::string DB = (commandlineArguments.count("db") != 0) ? commandlineArguments["db"] : "1030/2";
    const uint64_t MEM{(commandlineArguments.count("mem") != 0) ? static_cast<uint64_t>(std::stoi(commandlineArguments["mem"])) : 64UL*1024UL};
    const std::string BOX{(commandlineArguments["box"].size() != 0) ? commandlineArguments["box"] : "0,-2,2,2"}; // random driving maneuver
    const uint64_t START{(commandlineArguments.count("start") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["start"])) : 0};
    const uint64_t END{(commandlineArguments.count("end") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["end"])) : std::numeric_limits<uint64_t>::max()};
    const bool PRINTALL{commandlineArguments["printAll"].size() != 0};
    const bool PRINT{commandlineArguments["print"].size() != 0};
    const uint64_t MIN_DIFF_TIME{(commandlineArguments.count("minDiffTime") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["minDiffTime"])) * 1000UL * 1000UL : 50UL * 1000UL * 1000UL};
    const uint64_t MIN_DURATION{(commandlineArguments.count("minDuration") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["minDuration"])) * 1000UL * 1000UL : 200UL * 1000UL * 1000UL};
    const uint64_t MAX_DURATION{(commandlineArguments.count("maxDuration") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["maxDuration"])) * 1000UL * 1000UL : 3000UL * 1000UL * 1000UL};
    const bool VERBOSE{commandlineArguments["verbose"].size() != 0};

        std::vector<std::string> boxStrings = stringtoolbox::split(BOX, ',');
    if (4 == boxStrings.size()) {
        // Extract the bounding box and turn into floating point numbers.
        std::pair<float,float> boxBL;
        boxBL.first = std::stof(boxStrings.at(0));
        boxBL.second = std::stof(boxStrings.at(1));

        std::pair<float,float> boxTR;
        boxTR.first = std::stof(boxStrings.at(2));
        boxTR.second = std::stof(boxStrings.at(3)); 

    ////////////////////////////////////////////////////////////////////////////////
    uint64_t entryCNT = 0;

    std::vector<std::pair<int64_t, int64_t>> detectionBF = cabinet_queryManeuverBruteForcePrimitive
    (
      MEM, 
      CABINET, 
      DB, 
      PRINT, 
      boxBL, 
      boxTR, 
      MIN_DURATION, 
      MAX_DURATION, 
      MIN_DIFF_TIME, 
      entryCNT
    );
  
    if(PRINT) {
      int i=0;
      for(auto _temp : detectionBF) {
        i++;
        std::cout << i << ".: " << _temp.first << " -> " << _temp.second << ", d = " << (_temp.second - _temp.first)/(1000UL*1000UL) << "ms" << std::endl;
      }
      // std::cout << "BF: We detected " << detectionBF.size() << " Maneuvers" << std::endl;
    }
    }
  }
  return retCode;
}