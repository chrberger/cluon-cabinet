/*
 * Copyright (C) 2022  Lukas Birkemeyer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "cluon-complete.hpp"
#include "cabinet-SFC-query-experiment.hpp"
#include "DrivingStatus.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>

#include <chrono>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>


int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if ( (0 == commandlineArguments.count("cab_bf")) || (0 == commandlineArguments.count("cab_sfc"))) {
    std::cerr << argv[0] << " query a cabinet (an lmdb-based key/value-database)." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cab=myStore.cab [--mem=32024] --geobox=bottom-left-latitude,bottom-left-longitude,top-right-latitude,top-right-longitude" << std::endl;
    std::cerr << "         --cab_bf:    name of the database file for BruteForce query" << std::endl;
    std::cerr << "         --cab_sfc:    name of the database file for SFC query" << std::endl;
    std::cerr << "         --mem:    upper memory size for database in memory in GB, default: 64,000 (representing 64TB)" << std::endl;
    std::cerr << "         --thr:    lower threshold in morton space e.g. 31634000000 or 33400000000 (emergency braking); alternative to --geobox (thr is prioritized)" << std::endl;
    std::cerr << "         --geobox: return all timeStamps for GPS locations within this rectangle specified by bottom-left and top-right lat/longs" << std::endl;
    std::cerr << "         --aplx:   Applenix data required (e.g. Snowfox)? default: no (e.g. Voyager)" << std::endl;
    std::cerr << "         --start:  start time stamp" << std::endl;
    std::cerr << "         --end:    end time stamp" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cab=myStore.cab --geobox=57.679000,12.309931,57.679690,12.312700" << std::endl;
    retCode = 1;
  } else {    
    const std::string CABINET{commandlineArguments["cab_bf"]};
    const std::string CABINET_SFC{commandlineArguments["cab_sfc"]};
    const uint64_t MEM{(commandlineArguments["mem"].size() != 0) ? static_cast<uint64_t>(std::stoi(commandlineArguments["mem"])) : 64UL*1024UL};
    const bool VERBOSE{commandlineArguments["verbose"].size() != 0};
    const uint64_t THR{(commandlineArguments.count("thr") != 0) ? static_cast<uint64_t>(std::stol(commandlineArguments["thr"])) : 0};
    const std::string GEOBOX{commandlineArguments["geobox"]};
    const bool APLX{commandlineArguments["aplx"].size() != 0};
    const uint64_t START{(commandlineArguments.count("start") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["start"])) : 0};
    const uint64_t END{(commandlineArguments.count("end") != 0) ? static_cast<uint64_t>(std::stoll(commandlineArguments["end"])) : 0};

    std::cerr << "Start: " << START << ", End: " << END << std::endl;
    
    std::vector<std::string> geoboxStrings = stringtoolbox::split(GEOBOX, ',');
    std::pair<float,float> geoboxBL;
    std::pair<float,float> geoboxTR;
    if (4 == geoboxStrings.size()) {
      geoboxBL.first = std::stof(geoboxStrings.at(0));
      geoboxBL.second = std::stof(geoboxStrings.at(1));
      geoboxTR.first = std::stof(geoboxStrings.at(2));
      geoboxTR.second = std::stof(geoboxStrings.at(3));
    }

    std::pair<float,float> _fenceBL;
    std::pair<float,float> _fenceTR;


////////////////////////////////////////////////////////////////////////////////
// Check Database consitencies
/*
  MDB_env *env{nullptr};
  const int numberOfDatabases{100};
  const int64_t SIZE_DB = MEM * 1024UL * 1024UL * 1024UL;

  // lambda to check the interaction with the database.
  auto checkErrorCode = [_argv=argv](int32_t rc, int32_t line, std::string caller) {
    if (0 != rc) {
      std::cerr << "[" << _argv[0] << "]: " << caller << ", line " << line << ": (" << rc << ") " << mdb_strerror(rc) << std::endl; 
    }
    return (0 == rc);
  };

  if (!checkErrorCode(mdb_env_create(&env), __LINE__, "mdb_env_create")) {
    return retCode;
  }
  if (!checkErrorCode(mdb_env_set_maxdbs(env, numberOfDatabases), __LINE__, "mdb_env_set_maxdbs")) {
    mdb_env_close(env);
    return retCode;
  }
  if (!checkErrorCode(mdb_env_set_mapsize(env, SIZE_DB), __LINE__, "mdb_env_set_mapsize")) {
    mdb_env_close(env);
    return retCode;
  }
  // Database_BF
  if (!checkErrorCode(mdb_env_open(env, CABINET.c_str(), MDB_NOSUBDIR|MDB_RDONLY, 0600), __LINE__, "mdb_env_open")) {
    mdb_env_close(env);
    return retCode;
  }
  
  MDB_txn *txn{nullptr};
  MDB_dbi dbi{0};
  if (!checkErrorCode(mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn), __LINE__, "mdb_txn_begin")) {
    mdb_env_close(env);
    return retCode;
  }
  retCode = APLX ? mdb_dbi_open(txn, "533/0", 0 , &dbi) : mdb_dbi_open(txn, "1030/2", 0 , &dbi);
  if (MDB_NOTFOUND  == retCode) {
    if(APLX){std::clog << "[" << argv[0] << "]: No database '533/0' found in " << CABINET << "." << std::endl;}
    else{std::clog << "[" << argv[0] << "]: No database '1030/2' found in " << CABINET << "." << std::endl;}

    return retCode;
  }

  mdb_set_compare(txn, dbi, &compareMortonKeys);
  // Multiple values are stored by existing timeStamp in nanoseconds.
  mdb_set_dupsort(txn, dbi, &compareKeys);

  uint64_t numberOfEntries_BF{0};
  MDB_stat stat;
  if (!mdb_stat(txn, dbi, &stat)) {
    numberOfEntries_BF = stat.ms_entries;
  }
  if(APLX){std::clog << "[" << argv[0] << "]: Found " << numberOfEntries_BF << " entries in database '533/0' in " << CABINET << std::endl;}
  else{std::clog << "[" << argv[0] << "]: Found " << numberOfEntries_BF << " entries in database '1030/2' in " << CABINET << std::endl;}


 // Database_SFC
  if (!checkErrorCode(mdb_env_open(env, CABINET_SFC.c_str(), MDB_NOSUBDIR|MDB_RDONLY, 0600), __LINE__, "mdb_env_open")) {
    mdb_env_close(env);
    return retCode;
  }
  
  if (!checkErrorCode(mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn), __LINE__, "mdb_txn_begin")) {
    mdb_env_close(env);
    return retCode;
  }
  retCode = APLX ? mdb_dbi_open(txn, "533/0-morton", 0 , &dbi) : mdb_dbi_open(txn, "1030/2-morton", 0 , &dbi);
  if (MDB_NOTFOUND  == retCode) {
    if(APLX){std::clog << "[" << argv[0] << "]: No database '533/0-morton' found in " << CABINET_SFC << "." << std::endl;}
    else{std::clog << "[" << argv[0] << "]: No database '1030/2-morton' found in " << CABINET_SFC << "." << std::endl;}

    return retCode;
  }

  mdb_set_compare(txn, dbi, &compareMortonKeys);
  // Multiple values are stored by existing timeStamp in nanoseconds.
  mdb_set_dupsort(txn, dbi, &compareKeys);

  uint64_t numberOfEntries_SFC{0};

  if (!mdb_stat(txn, dbi, &stat)) {
    numberOfEntries_SFC = stat.ms_entries;
  }
  if(APLX){std::clog << "[" << argv[0] << "]: Found " << numberOfEntries_SFC << " entries in database '533/0-morton' in " << CABINET_SFC << std::endl;}
  else{std::clog << "[" << argv[0] << "]: Found " << numberOfEntries_SFC << " entries in database '1030/2-morton' in " << CABINET_SFC << std::endl;}
*/

////////////////////////////////////////////////////////////////////////////////

    _fenceBL.first = -1.5; _fenceBL.second = 0.0;
    _fenceTR.first = 0.75; _fenceTR.second = 5;
    DrivingStatus *leftCurve  = new DrivingStatus( "leftCurve",
            _fenceBL,
            _fenceTR,
            500000000,
            3000000000,
            -200000000,
            2000000000,
            50000000);

    _fenceBL.first = -1.5; _fenceBL.second = -5;
    _fenceTR.first = 0.75; _fenceTR.second = -0.0;
    DrivingStatus *rightCurve = new DrivingStatus( "rightCurve",
            _fenceBL,
            _fenceTR,
            500000000,
            3000000000,
            -200000000,
            2000000000,
            50000000);
    
    _fenceBL.first = 4; _fenceBL.second = -4;
    _fenceTR.first = 10; _fenceTR.second = 4;
    DrivingStatus *harsh_braking = new DrivingStatus( "harsh_braking",
            _fenceBL,
            _fenceTR,
            500000000,
            3000000000,
            -200000000,
            2000000000,
            50000000);

    //std::vector<DrivingStatus*> maneuver;
    
    //maneuver.push_back(leftCurve);
    //maneuver.push_back(rightCurve);
    //maneuver.push_back(harsh_braking);

////////////////////////////////////////////////////////////////////////////////

  std::fstream resultDatei("results.txt", std::ios::out); //out: zum schreiben oeffnen

  resultDatei << "ID" << ", " << "db_start" << ", " << "db_end" << ", " << "entryCNT" << ", " << "duration_BF_primitive" << ", " << "duration_BF" << ", " <<  "duration_SFC" << ", "
         << "detection_BF_primitive.size()" << ", " << "detection_BF.size()" << ", " << "detection_SFC.size()" << ", "
         <<  "false_negatives_BF.size()" << ", " <<  "false_positives_BF.size()" << ", "
         << "false_negatives_SFC.size()" << ", " << "false_positives_SFC.size()" << ", " << "maneuver.size()" << ", "
         << "BL_x_1" << ", " << "BL_y_1" << ", " << "TR_x_1" << ", " << "TR_y_1" << ", "
         << "BL_x_2" << ", " << "BL_y_2" << ", " << "TR_x_2" << ", " << "TR_y_2" << ", "
         << "BL_x_3" << ", " << "BL_y_3" << ", " << "TR_x_3" << ", " << "TR_y_3" << std::endl;

  int noRand = 5;//5
  int noStages = 3;
  int noDbSize = 20; // 10
  int testCnt = 1;

  float min_x = -8.0f;
  float max_x = 8.0f;
  float min_y = -6.0f;
  float max_y = 6.0f;

  // uint64_t db_min =  1580389401359941000; // großer Datensatz (server)
  // uint64_t db_max = 1646827840115619000;

  uint64_t db_min = START; // großer Datensatz (server)
  uint64_t db_max = END;

  //uint64_t db_min =  1645098077131594000; // kleiner Datensatz (lokal)
  //uint64_t db_max = 1645101199045973000; 

  int maxTest = noRand*noStages*noDbSize;

  uint64_t db_start;
  uint64_t db_end;

  int i = 1;

  //parallel_for(noDbSize, [&](int start, int end){ 
  //  for(int i = start; i < end; ++i){
      //std::cout << "test" << i << std::endl;

  std::cout << std:: endl << "-------------------- Fuzz relevant SearchMasks ------------------------" << std::endl;

  std::vector<std::vector<DrivingStatus*>> maneuverList;
  std::vector<DrivingStatus*> maneuver;

  
  int32_t oldPercentage{-1};
  uint64_t entries{0};

  for(int cnt = 0; cnt < noRand; cnt++) {
    
    for(int _curStage = 1; _curStage <= noStages; _curStage++) {

      int fenceTry = 0;
      do {
        maneuver.clear();
        for(int _stages=0; _stages < _curStage; _stages++) {

          _fenceBL.first = float_rand( min_x,  max_x-0.1 );
          _fenceBL.second = float_rand( min_y,  max_y-0.1 );
          _fenceTR.first = float_rand( _fenceBL.first,  max_x );
          _fenceTR.second = float_rand( _fenceBL.second,  max_y );

          DrivingStatus *manStage  = new DrivingStatus( "Maneuver stage",
                  _fenceBL,
                  _fenceTR,
                  200000000, // 500000000
                  3000000000,
                  -200000000,
                  2000000000,
                  50000000);

          maneuver.push_back (manStage);
        }
        fenceTry++;
        if(fenceTry > 750) {
          std::cout << "abbort criteria: more than 500 trys... proceed with bad search mask seed" << std::endl;
          //goto resetDatabase;
          break;
        }
      } while((identifyManeuversSFC(argv, CABINET_SFC, MEM, VERBOSE, THR, APLX, geoboxStrings, geoboxBL, geoboxTR, maneuver, db_min, db_max).size() == 0));
      maneuverList.push_back(maneuver);

      entries++;

      const int32_t percentage = static_cast<int32_t>((static_cast<float>(entries) * 100.0f) / static_cast<float>(noRand*noStages));
      if ((percentage != oldPercentage)) {
        std::clog <<"Processed " << percentage << "% (" << entries << "/" << noRand*noStages << " Masks)" << std::endl;
        oldPercentage = percentage;
      }
            
    }
    //std::cout << " ------------------- Fuzz SearchMasks Stage " << _curStage << " --------------" << std::endl;
  }

  std::cout << std:: endl << "-------------------- Fire up Testengine ------------------------" << std::endl;

  /*for (auto _man :maneuverList) {
    for(auto tempMan : _man) {
        std::cout << std::endl << "BL: (" << tempMan->fenceBL.first << "," << tempMan->fenceBL.second << ") TR: (" << tempMan->fenceTR.first << "," << tempMan->fenceTR.second  << ")" << std::endl;
      }
    std::cout << " -------------------" << std::endl;
  }*/


  for(int db_sizeCNT = 0; db_sizeCNT < noDbSize; db_sizeCNT++) {

    //resetDatabase:

    if(db_sizeCNT == 0) {
      db_start = db_min;
      db_end = db_max;
    } else {
      db_start = ts_rand(db_min, db_max-1);
      db_end = ts_rand(db_start, db_max);
    }
    
    //for(int cur_stage = 1; cur_stage <= noStages; cur_stage++) {

    
    for(auto maneuver: maneuverList) {
      ////////////////////////////////////////////////////////////////////////////////

          
          std::cout << std::endl << "------------------------------------ Test " << testCnt << "/" << maxTest << " -----------------------------"  << std::endl;
          
          //std::vector<DrivingStatus*> maneuver;

          //std::cout << "Fuzz relevant SearchMask" << std::endl;

          // int fenceTry = 0;
          // do {
          //   maneuver.clear();
          //   for(int _stages=0; _stages < cur_stage; _stages++) {

          //     _fenceBL.first = float_rand( min_x,  max_x-0.1 );
          //     _fenceBL.second = float_rand( min_y,  max_y-0.1 );
          //     _fenceTR.first = float_rand( _fenceBL.first,  max_x );
          //     _fenceTR.second = float_rand( _fenceBL.second,  max_y );

          //     DrivingStatus *manStage  = new DrivingStatus( "Maneuver stage",
          //             _fenceBL,
          //             _fenceTR,
          //             200000000, // 500000000
          //             3000000000,
          //             -200000000,
          //             2000000000,
          //             50000000);

          //     maneuver.push_back (manStage);
          //   }
          //   fenceTry++;
          //   if(fenceTry > 200) {
          //     std::cout << "abbort criteria: more than 200 trys... proceed with bad search mask seed" << std::endl;
          //     //goto resetDatabase;
          //     break;
          //   }
          // } while((identifyManeuversSFC(argv, CABINET_SFC, MEM, VERBOSE, THR, APLX, geoboxStrings, geoboxBL, geoboxTR, maneuver, db_start, db_end).size() == 0));


          for(auto tempMan : maneuver) {
            std::cout << "BL: (" << tempMan->fenceBL.first << "," << tempMan->fenceBL.second << ") TR: (" << tempMan->fenceTR.first << "," << tempMan->fenceTR.second  << ")" << std::endl;
          }

          
      ////////////////////////////////////////////////////////////////////////////////

          uint64_t entryCNT = 0;

          std::cout << std::endl << "Brute Force Primitive" << std::endl;

          auto start = std::chrono::high_resolution_clock::now();
          // std::vector<std::pair<int64_t, int64_t>> detection_BF_primitive = cabinet_queryManeuverBruteForcePrimitive(MEM, CABINET, APLX, VERBOSE, _fenceBL, _fenceTR, maneuver, db_start, db_end, entryCNT);
          auto end = std::chrono::high_resolution_clock::now();
          int64_t duration_BF_primitive = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
          std::cout << "Done!" << std::endl;

          std::cout << std::endl << "Brute Force" << std::endl;

          start = std::chrono::high_resolution_clock::now();
          std::vector<std::pair<int64_t, int64_t>> detection_BF = cabinet_queryManeuverBruteForce(MEM, CABINET, APLX, VERBOSE, _fenceBL, _fenceTR, maneuver, db_start, db_end);
          end = std::chrono::high_resolution_clock::now();
          int64_t duration_BF = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
          std::cout << "Done!" << std::endl;

          std::cout << std::endl << "Space Filling Curve" << std::endl;

          start = std::chrono::high_resolution_clock::now();
          std::vector<std::pair<int64_t, int64_t>> detection_SFC = identifyManeuversSFC(argv, CABINET_SFC, MEM, VERBOSE, THR, APLX, geoboxStrings, geoboxBL, geoboxTR, maneuver, db_start, db_end);
          end = std::chrono::high_resolution_clock::now();
          int64_t duration_SFC = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();

          std::cout << "Done!" << std::endl << std::endl;

          // std::vector<std::pair<int64_t, int64_t>>false_negatives_BF = getFalseNegatives(detection_BF_primitive, detection_BF);
          // std::vector<std::pair<int64_t, int64_t>>false_positives_BF = getFalsePositives(detection_BF_primitive, detection_BF);

          // std::vector<std::pair<int64_t, int64_t>>false_negatives_SFC = getFalseNegatives(detection_BF_primitive, detection_SFC);
          // std::vector<std::pair<int64_t, int64_t>>false_positives_SFC = getFalsePositives(detection_BF_primitive, detection_SFC);

      ////////////////////////////////////////////////////////////////////////////////
      // Output all
          /*if(!detection_BF.empty()) {
            for(auto _temp : detection_BF) {
              std::cout << "BF:  Maneuver detected at: " << _temp.first << ", " << _temp.second << std::endl;
            }
          }
          if(!detection_SFC.empty()) {
            for(auto _temp : detection_SFC) {
              std::cout << "SFC: Maneuver detected at: " << _temp.first << ", " << _temp.second << std::endl;
            }
          }*/

      ////////////////////////////////////////////////////////////////////////////////
      //     std::cout << "Database" << std::endl;
      //     std::cout << "Start TS: " << db_start << " End TS: " << db_end << std::endl;
      //     std::cout << "No. of entries in database: " << entryCNT << std::endl;

      //     // ausgabe für HMI
      //     std::cout << std::endl << "Effectivity" << std::endl;
      //     std::cout << "BF-Primitiv: We detected " << detection_BF_primitive.size() << " Maneuvers" << std::endl;
      //     std::cout << "BF:          We detected " << detection_BF.size() << " Maneuvers" << std::endl;
      //     std::cout << "SFC:         We detected " << detection_SFC.size() << " Maneuvers" << std::endl;

      //     // std::cout << "Effectivity" << std::endl;

      //     // if(detection_BF.size() != 0) {
      //     //   float detShare = (static_cast<float>(detection_BF.size())-static_cast<float>(false_negatives.size()))/static_cast<float>(detection_BF.size()) * 100.0f;
      //     //   std::cout << "(" << false_negatives.size() << " false negatives) : " << detection_BF.size()-false_negatives.size() << "/" << detection_BF.size() <<  " (" << detShare << "%)" <<" elements are detected by SFC-query" << std::endl;
      //     // }
      //     // else
      //     //   std::cout << "(" << false_negatives.size() << " false negatives) : " << detection_BF.size()-false_negatives.size() << "/" << detection_BF.size() << " elements are detected by SFC-query" << std::endl;

      //     // std::cout << "(" << false_positives.size() << " false positives) : " << false_positives.size() << " elements are additionally detected by SFC-query." << std::endl;

      //     std::cout << std::endl << "Efficiency" << std::endl;

      //     std::cout << "BF-Primitiv: " << duration_BF_primitive << " ms" << std::endl;
      //     std::cout << "BF         : " << duration_BF << " ms" << std::endl;
      //     std::cout << "SFC        : " << duration_SFC << " ms" << std::endl;
      // ////////////////////////////////////////////////////////////////////////////////
      //     //resultDatei
      //     resultDatei << testCnt << ", " << db_start << ", " << db_end << ", " << entryCNT << ", " << duration_BF_primitive << ", " << duration_BF << ", " <<  duration_SFC << ", "
      //       << detection_BF_primitive.size() << ", " << detection_BF.size() << ", " << detection_SFC.size() << ", "
      //       <<  false_negatives_BF.size() << ", " <<  false_positives_BF.size() << ", "
      //       << false_negatives_SFC.size() << ", " << false_positives_SFC.size() << ", " << maneuver.size();

      //     //std::string temp;
      //     //temp = i;
      //     // + ", " + duration_BF_primitive + ", " + duration_BF + ", " +  duration_SFC + ", "
      //     //  + detection_BF_primitive.size() + ", " + detection_BF.size() + ", " + detection_SFC.size() + ", "
      //     //  +  false_negatives_BF.size() + ", " +  false_positives_BF.size() + ", " + false_negatives_SFC.size() + ", " + false_positives_SFC.size()+<< ", ";     
          
      //     for(auto tempMan : maneuver) {
      //       resultDatei << ", " << tempMan->fenceBL.first << ", " << tempMan->fenceBL.second << ", "
      //         << tempMan->fenceTR.first << ", " << tempMan->fenceTR.second;
      //     }
      //     resultDatei << std::endl;

      //     testCnt++;
        }
      }
    //}
    //}});
  }
  

  std::cout << std::endl << "Please find all results in result.txt" << std::endl;

  return retCode;
}
