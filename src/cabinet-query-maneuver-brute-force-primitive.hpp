/*
 * Copyright (C) 2022  Lukas Birkemeyer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef CABINETQUERYMANEUVERBRUTEFORCEPRIMITIVE_HPP
#define CABINETQUERYMANEUVERBRUTEFORCEPRIMITIVE_HPP

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"
#include "key.hpp"
#include "morton.hpp"
#include "lmdb++.h"
#include "lz4.h"
#include "DrivingStatus.hpp"

#include <iostream>
#include <sstream>
#include <string>


inline std::vector<std::pair<int64_t,int64_t>> detectSingleManeuver_primitive(std::vector<int64_t> * _tempDrivingStatusList, int64_t minDiffTime, int64_t minDuration, int64_t maxduration) {
  int32_t retCode{0};
  
  //not required, since original database is sorted
  //sort(_tempDrivingStatusList->begin(), _tempDrivingStatusList->end(), cmp_sort);

  int64_t _tsStart = 0;
  int64_t _tsEnd = 0;

  std::vector<std::pair<int64_t,int64_t>> _singleManeuverList;

  for(int i=0; i < _tempDrivingStatusList->size(); i++) {
    if(0==i){
      _tsStart = (*_tempDrivingStatusList)[i];
      continue;
    }

    if((minDiffTime < (*_tempDrivingStatusList)[i] - (*_tempDrivingStatusList)[i-1]) || (i == (_tempDrivingStatusList->size()-1))) {
      
      _tsEnd = (*_tempDrivingStatusList)[i-1];

      if(i == (_tempDrivingStatusList->size()-1) && (minDiffTime > (*_tempDrivingStatusList)[i] - (*_tempDrivingStatusList)[i-1]))
      {
        _tsEnd = (*_tempDrivingStatusList)[i];
      }

      
      int64_t duration = _tsEnd - _tsStart;
      
      if((duration > minDuration) && (duration < maxduration)) {
        std::pair<int64_t,int64_t> _tempMan;
        _tempMan.first = _tsStart;
        _tempMan.second = _tsEnd;

        _singleManeuverList.push_back(_tempMan);
      }

      _tsStart = (*_tempDrivingStatusList)[i];
      // TODO: irgendwas mit Start ist kaputt
    }
  }

  return _singleManeuverList;
}

// namespace brute_force {

  inline bool in_fence_primitive(const std::pair<float,float> _fenceBL, const std::pair<float,float> _fenceTR, const float _x, const float _y) {
    if((_x >= _fenceBL.first) && (_x <= _fenceTR.first)){
      if((_y >= _fenceBL.second) && (_y <= _fenceTR.second)){
        return true;
      }
    }
    return false;
  }

  inline int64_t maneuverDetectorRecursiv_primtive(std::vector<DrivingStatus*> maneuver, int maneuver_idx, int status_idx) {
  
    if(maneuver.size() == 1) {
      return maneuver[maneuver_idx]->singleManeuverList[status_idx].second;
    }

    if( maneuver_idx >= maneuver.size())
      return -1;

    //if(maneuver.size() == 1)
    //  return 0;

    // std::cout << "start maneuver Detection: maneuver idx " << maneuver_idx << " status " << status_idx << std::endl;
    
    DrivingStatus* currentManeuver = maneuver[maneuver_idx];
    DrivingStatus* nextManeuver = maneuver[maneuver_idx + 1];

    if(nextManeuver->singleManeuverList.size() == 0)
      return -1;

    std::pair<int64_t,int64_t> currentStatus = currentManeuver->singleManeuverList[status_idx];

    //for(std::pair<int64_t,int64_t> nextStatus : nextManeuver->singleManeuverList) 
    
    for(int i = 0; i <= nextManeuver->singleManeuverList.size(); i++) {

      std::pair<int64_t,int64_t> nextStatus = nextManeuver->singleManeuverList[i];

      int64_t gap = nextStatus.first - currentStatus.second;

      //std::cout << "Gap: " << gap << " first " << nextStatus.first << " end " << currentStatus.second << std::endl;

      if(gap > currentManeuver->maxGap){
        // std::cout << "gap " << gap << " groesser als " <<  currentManeuver->maxGap << std::endl;
        break;
      }

      if (gap < currentManeuver->minGap) continue;

      if((gap > currentManeuver->minGap) && (gap < currentManeuver->maxGap)) {
        
        if(maneuver_idx == (maneuver.size() - 2)) {
          return nextStatus.second;
        } else {
          return maneuverDetectorRecursiv_primtive(maneuver, maneuver_idx + 1, i);
        }
      }
    }

    return -1;
  }

  
  inline std::vector<std::pair<int64_t, int64_t>> cabinet_queryManeuverBruteForcePrimitive
  (
    const uint64_t &MEM, 
    const std::string &CABINET, 
    const std::string &DB,
    const bool &PRINT, 
    const std::pair<float,float> boxBL, 
    const std::pair<float,float> boxTR, 
    const uint64_t MIN_DURATION,
    const uint64_t MAX_DURATION,
    const uint64_t MIN_DIFF_TIME,
    uint64_t &cntEntries
  )
  {
    bool failed{false};

    // Maneuver Detection

    std::pair<int64_t, int64_t> fullManeuver;
    std::vector<std::pair<int64_t, int64_t>> fullManeuverList;
    std::vector<DrivingStatus*> _maneuver;
    DrivingStatus *maneuver = new DrivingStatus( "",
            boxBL,
            boxTR,
            MIN_DURATION,
            MAX_DURATION,
            -200000000,
            2000000000,
            MIN_DIFF_TIME);

    _maneuver.push_back(maneuver);

    try {

      uint16_t _ID = opendlv::proxy::AccelerationReading::ID();

      auto env = lmdb::env::create();
      env.set_mapsize(MEM * 1024UL * 1024UL * 1024UL);
      env.set_max_dbs(100);
      env.open(CABINET.c_str(), MDB_NOSUBDIR, 0600);

      // Fetch key/value pairs in a read-only transaction.
      auto rotxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbiAll = lmdb::dbi::open(rotxn, "all");
      dbiAll.set_compare(rotxn, &compareKeys);
      
      auto dbi = lmdb::dbi::open(rotxn, DB.c_str());
      dbi.set_compare(rotxn, &compareKeys);

      MDB_val key;
      MDB_val value;

      bool firstFlag = true;

      for(DrivingStatus* _tempDS : _maneuver) {

        std::vector<int64_t> _tempDrivingStatusList;

        auto cursor = lmdb::cursor::open(rotxn, dbi);

        while (cursor.get(&key, &value, MDB_NEXT)) {
          
          MDB_val keyAll = key;
          MDB_val valueAll = value;

          // if we dump another table than "all", we need to look up the actual values from the original "all" table first.
          if (DB != "all") {
            keyAll = key;

            if (!lmdb::dbi_get(rotxn, dbiAll, &keyAll, &valueAll)) {
              continue;
            }
          }

          const char *ptr = static_cast<char*>(keyAll.mv_data);
          cabinet::Key storedKey = getKey(ptr, keyAll.mv_size);

          std::vector<char> val;
          val.reserve(storedKey.length());
          if (storedKey.length() > valueAll.mv_size) {
            LZ4_decompress_safe(static_cast<char*>(valueAll.mv_data), val.data(), valueAll.mv_size, val.capacity());
          }
          else {
            // Stored value is uncompressed.
            memcpy(val.data(), static_cast<char*>(valueAll.mv_data), valueAll.mv_size);
          }

          // Extract an Envelope and its payload on the example for AccelerationReading
          std::stringstream sstr{std::string(val.data(), storedKey.length())};
          auto e = cluon::extractEnvelope(sstr);
          if (e.first && e.second.dataType() == _ID) {

            if(firstFlag) cntEntries++;

            float _currAccelLon = 0;
            float _currAccelTrans = 0;
            
            const auto tmp = cluon::extractMessage<opendlv::proxy::AccelerationReading>(std::move(e.second));
            _currAccelLon = tmp.accelerationX();
            _currAccelTrans = tmp.accelerationY();

            _currAccelLon = std::lroundf(_currAccelLon * 100.0f) / 100.0f;
            _currAccelTrans = std::lroundf(_currAccelTrans * 100.0f) / 100.0f;

            if(in_fence_primitive(_tempDS->fenceBL, _tempDS->fenceTR, _currAccelLon, _currAccelTrans) == true) {
              // todo
              _tempDrivingStatusList.push_back(storedKey.timeStamp());
            }
          }
        }
        _tempDS->singleManeuverList = detectSingleManeuver_primitive(&_tempDrivingStatusList, _tempDS->minDiffTime, _tempDS->minDuration, _tempDS->maxDuration);

        cursor.close();
        firstFlag = false;
      }

      for(int status_idx = 0; status_idx < _maneuver[0]->singleManeuverList.size(); status_idx++) {
        fullManeuver.second = maneuverDetectorRecursiv_primtive(_maneuver, 0, status_idx);

        if(fullManeuver.second != -1){
          fullManeuver.first = _maneuver[0]->singleManeuverList[status_idx].first;
          
          fullManeuverList.push_back(fullManeuver);
        }
      }
      rotxn.abort();
      return fullManeuverList;
    }

    catch (...) {
      failed = true;
    }
  }

#endif
