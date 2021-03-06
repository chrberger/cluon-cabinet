/*
 * Copyright (C) 2021  Christian Berger
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "cluon-complete.hpp"
#include "cabinet2rec.hpp"

#include <iostream>
#include <iomanip>
#include <string>

struct space_out : std::numpunct<char> {
  char do_thousands_sep()   const { return ','; }  // separate with spaces
  std::string do_grouping() const { return "\3"; } // groups of 3 digit
};

int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if (0 == commandlineArguments.count("cab")) {
    std::cerr << argv[0] << " exports all entries from the 'all' table of a cabinet (an lmdb-based key/value-database) as Envelopes to a .rec-file." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cab=myStore.cab [--rec=myFile.rec] [--mem=32024] [--start=startTime] [--end=endTime] [--verbose]" << std::endl;
    std::cerr << "         --cab:     name of the database file" << std::endl;
    std::cerr << "         --rec:     name of the rec file (optional; otherwise, a new file based on the .cab file with .rec as suffix is created)" << std::endl;
    std::cerr << "         --mem:     upper memory size for database in memory in GB, default: 64,000 (representing 64TB)" << std::endl;
    std::cerr << "         --start:   start time of the export in Unix epoch seconds; default: 0" << std::endl;
    std::cerr << "         --end:     end time of the export in Unix epoch seconds; default: inf" << std::endl;
    std::cerr << "         --verbose: display information" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cab=myStore.cab --rec=myRecFile.rec" << std::endl;
    retCode = 1;
  } else {
    std::clog.imbue(std::locale(std::cout.getloc(), new space_out));

    const std::string CABINET{commandlineArguments["cab"]};
    const std::string REC{(commandlineArguments["rec"].size() != 0) ? commandlineArguments["rec"] : "./" + CABINET + ".rec"};
    const uint64_t MEM{(commandlineArguments["mem"].size() != 0) ? static_cast<uint64_t>(std::stoi(commandlineArguments["mem"])) : 64UL*1024UL};
    const int64_t START{(commandlineArguments["start"].size() != 0) ? static_cast<int64_t>(std::stoi(commandlineArguments["start"])) : 0};
    const int64_t END{(commandlineArguments["end"].size() != 0) ? static_cast<int64_t>(std::stoi(commandlineArguments["end"])) : std::numeric_limits<int64_t>::max()};
    const bool VERBOSE{(commandlineArguments["verbose"].size() != 0)};

    const std::string ARGV0{argv[0]};
    retCode = cabinet2rec(ARGV0, MEM, CABINET, REC, START, END, VERBOSE);
  }
  return retCode;
}
