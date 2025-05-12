/*
 * Copyright (C) 2022  Christian Berger
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef CABINETWGS84TOTRIPS_HPP
#define CABINETWGS84TOTRIPS_HPP

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"
#include "key.hpp"
#include "morton.hpp"
#include "lmdb++.h"
#include "lz4.h"
#include "geofence.hpp"
#include "WGS84toCartesian.hpp"

#include <cmath>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

inline bool cabinet_WGS84toTrips(const uint64_t &MEM, const std::string &CABINET, const uint32_t &senderStamp, std::vector<std::array<double,2>> polygon, const std::string &TRIPSCABINET, const bool &GPX, const uint32_t &MIN_LEN, const uint32_t &MAX_LEN, const bool &VERBOSE) {
  bool failed{false};
  try {
    auto env = lmdb::env::create();
    env.set_mapsize(MEM/2 * 1024UL * 1024UL * 1024UL);
    env.set_max_dbs(100);
    env.open(CABINET.c_str(), MDB_NOSUBDIR, 0600);

    auto envout = lmdb::env::create();
    envout.set_mapsize(MEM/2 * 1024UL * 1024UL * 1024UL);
    envout.set_max_dbs(100);
    envout.open(TRIPSCABINET.c_str(), MDB_NOSUBDIR, 0600);

    // Fetch key/value pairs in a read-only transaction.
    auto rotxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    auto dbi = lmdb::dbi::open(rotxn, "all");
    dbi.set_compare(rotxn, &compareKeys);
    const uint64_t totalEntries = dbi.size(rotxn);
    std::cerr << "Found " << totalEntries << " entries." << std::endl;
    auto cursor = lmdb::cursor::open(rotxn, dbi);
    MDB_val key;
    MDB_val value;

    int32_t oldPercentage{-1};
    uint64_t entries{0};
    uint64_t skipped{0};
    uint64_t kept{0};
    uint64_t invalid{0};

    int64_t tripStart{0};
    cabinet::Key keyTripStart;
    cabinet::Key keyTripEnd;
    std::array<double,2> posTripStart{0,0};
    std::array<double,2> posTripEnd{0,0};

    bool firstValidGPSLocationStored = false;

    // GPX export.
    std::array<double,2> lastPos{0,0};
    std::array<double,2> currentPos{0,0};
    std::string filenameGPX;
    const char *GPX_HEADER1 = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.0">
  <name>)";
    const char *GPX_HEADER2 = R"(</name>
<trk><name>GPX export</name><number>1</number><trkseg>
)";
    const char *GPX_FOOTER = R"(</trkseg></trk>
</gpx>
)";

    std::stringstream sstrGPX;

    std::vector<std::pair<std::vector<char>, std::vector<char> > > bufferedKeyValues;
    std::vector<std::pair<std::vector<char>, std::vector<char> > > bufferOfKeyValuePairsToStore;
    while (cursor.get(&key, &value, MDB_NEXT)) {
      entries++;
      const char *ptr = static_cast<char*>(key.mv_data);
      cabinet::Key storedKey = getKey(ptr, key.mv_size);
      std::vector<char> val;
      val.reserve(storedKey.length());
      if (storedKey.length() > value.mv_size) {
        LZ4_decompress_safe(static_cast<char*>(value.mv_data), val.data(), value.mv_size, val.capacity());
      }
      else {
        // Stored value is uncompressed.
        memcpy(val.data(), static_cast<char*>(value.mv_data), value.mv_size);
      }

      if (  (storedKey.dataType() == opendlv::proxy::GeodeticWgs84Reading::ID())
         && (storedKey.senderStamp() == senderStamp) ) {
        std::stringstream sstr{std::string(val.data(), storedKey.length())};
        auto e = cluon::extractEnvelope(sstr);
        if (e.first) {
          // Extract value from Envelope and check whether location is within polygon.
          const auto tmp = cluon::extractMessage<opendlv::proxy::GeodeticWgs84Reading>(std::move(e.second));
          std::array<double,2> pos{tmp.latitude(),tmp.longitude()};
          bool inside = geofence::isIn<double>(polygon, pos);
          if (VERBOSE) {
            std::cout << tmp.latitude() << "," << tmp.longitude() << ": " << inside << std::endl;
          }

          if (inside) {
            // The current GPS position is inside the allowed geo-fence.

            // If a GPS location was never stored into the bufferOfKeyValuePairsToStore, we need to clear it as all samples occurred so far are outside the geo fence.
            if (!firstValidGPSLocationStored) {
              skipped += bufferOfKeyValuePairsToStore.size();
              bufferOfKeyValuePairsToStore.clear();
            }

            // 1. Add the current data sample (GPS) to the bufferOfKeyValuePairsToStore.
            {
              std::vector<char> keyToBuffer;
              keyToBuffer.reserve(key.mv_size);
              std::copy(static_cast<char*>(key.mv_data), static_cast<char*>(key.mv_data) + key.mv_size, std::back_inserter(keyToBuffer));

              std::vector<char> valueToBuffer;
              valueToBuffer.reserve(value.mv_size);
              std::copy(static_cast<char*>(value.mv_data), static_cast<char*>(value.mv_data) + value.mv_size, std::back_inserter(valueToBuffer));

              auto p = std::make_pair(keyToBuffer, valueToBuffer);
              bufferOfKeyValuePairsToStore.push_back(p);

              // Flag that we stored the first GPS location.
              firstValidGPSLocationStored = true;
            }

            // 2. Transfer all temporarily buffered data samples to the final buffer we need to eventually add to the database.
            std::copy(bufferOfKeyValuePairsToStore.begin(), bufferOfKeyValuePairsToStore.end(), std::back_inserter(bufferedKeyValues));

            // 3. Clear temporarily buffered data samples so we can store the next data samples that temporally come after this GPS position,
            // for which we cannot decide yet whether they need to be discarded or not.
            bufferOfKeyValuePairsToStore.clear();

            // We found the beginning of a trip.
            if (0 == tripStart) {
              tripStart = storedKey.timeStamp();
              keyTripStart = storedKey;
              posTripStart = pos;

              // Check whether we need to spit out GPX data for this trip.
              if (GPX) {
                lastPos[0] = lastPos[1] = 0; 
                sstrGPX.str("");
                sstrGPX << std::string(GPX_HEADER1) << "GPX-Trace-" << tripStart << std::string(GPX_HEADER2);
              }
            }

            // Always store the current position to end the trace smoothly.
            currentPos = pos;

            // Check whether we need to spit out GPX data for this trip.
            if (GPX) {
              struct tm ts;
              char buf[80];
              // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
              time_t timestamp = e.second.sampleTimeStamp().seconds();
              ts = *localtime(&timestamp);
              strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ts);
              std::string t(buf);

              if (filenameGPX.size() == 0) {
                char _buf[80];
                strftime(_buf, sizeof(_buf), "%Y-%m-%dT%H%M%SZ", &ts);
                std::stringstream sstrFilename;
                sstrFilename << std::string(_buf) << "-" << tripStart << ".gpx";
                filenameGPX = sstrFilename.str();
              }

              // Initial position.
              if (fabs(lastPos[0]+lastPos[1]) < 1e-4) {
                lastPos = pos;
                sstrGPX << "<trkpt lat=\"" << std::setprecision(8) << pos[0] << "\" lon=\"" << pos[1] << "\">"
                        << "<ele>" << 0 << "</ele><time>" << t << "</time></trkpt>" << std::endl;
              }
              else {
                // Compute distance between last position and current position to be larger than 2m.
                std::array<double, 2> cartesianPosition = wgs84::toCartesian(lastPos, pos);
                double len = sqrt(cartesianPosition[0]*cartesianPosition[0] + cartesianPosition[1]*cartesianPosition[1]);
                const double TwoMeters = 2.0;
                if (len > TwoMeters) {
                  lastPos = pos;
                  sstrGPX << "<trkpt lat=\"" << std::setprecision(8) << pos[0] << "\" lon=\"" << pos[1] << "\">"
                          << "<ele>" << 0 << "</ele><time>" << t << "</time></trkpt>" << std::endl;
                }
              }
            }

            // Always store the current key as keyTripEnd as this key could be the last valid GPS position within the geofence.
            keyTripEnd = storedKey;
            posTripEnd = pos;
          }
          else {
            // This GPS coordinate is now outside the allowed GPS fence and we have to check what to do with the buffereed key/values.

            // 1. Clear the bufferOfKeyValuePairsToStore as all these data samples were received after the last GPS position that was inside the allowed geo fence.
            skipped += bufferOfKeyValuePairsToStore.size();
            bufferOfKeyValuePairsToStore.clear();

            if (bufferedKeyValues.size() > 0) {
              std::array<double, 2> cartesianPosition = wgs84::toCartesian(posTripStart, currentPos);
              const double len = sqrt(cartesianPosition[0]*cartesianPosition[0] + cartesianPosition[1]*cartesianPosition[1]);
              //std::cout << "Len = " << len << std::endl;
              // 2. If the bufferedKeyValues meet the MIN_LEN, MAX_LEN criteria and is shorter than 24h, do:
              const int64_t tripEnd = keyTripEnd.timeStamp();
              if ( ((MIN_LEN < len) && (len < MAX_LEN))
                 && (24*3600 > ((tripEnd - tripStart)/(1000UL*1000UL*1000UL))) ) {
                // This trip is meeting the minimum/maximum length criteria.

                // 2.1 Store those values to the database.
                std::cout << "Trip of length: " << (len/1000.0) << "km with " << bufferedKeyValues.size() << " entries." << std::endl;
                uint32_t i = 0;

                // Create transactions.
                auto txndbAll = lmdb::txn::begin(envout);
                auto dbAll = lmdb::dbi::open(txndbAll, "all", MDB_CREATE);
                dbAll.set_compare(txndbAll, &compareKeys);

                bool dbGeodeticWgs84SenderStamp_open = false;
                auto dbGeodeticWgs84SenderStamp = lmdb::dbi::open(txndbAll, "txnWgs84tmp", MDB_CREATE|MDB_DUPSORT|MDB_DUPFIXED);

                bool dbDataTypeSenderStamp_open = false;
                auto dbDataTypeSenderStamp = lmdb::dbi::open(txndbAll, "txnDTTStmp", MDB_CREATE);

                for (auto f : bufferedKeyValues) {
                  //std::cout << __LINE__ << ": " << i << "/" << bufferedKeyValues.size() << std::endl;
                  i++;

                  // Store buffered key/value in database "all".
                  {
                    MDB_val _key;
                    _key.mv_size = f.first.size();
                    _key.mv_data = f.first.data();

                    MDB_val _value;
                    _value.mv_size = f.second.size();
                    _value.mv_data = f.second.data();

                    {
                      // Store key/value in db "all".
                      lmdb::dbi_put(txndbAll, dbAll.handle(), &_key, &_value, 0); 
                    }

                    // 2.2 Compute the Morton codes if the current key contains a GPS location.
                    {
                      const char *_ptr = static_cast<char*>(_key.mv_data);
                      cabinet::Key _storedKey = getKey(_ptr, _key.mv_size);

                      if (_storedKey.dataType() == opendlv::proxy::GeodeticWgs84Reading::ID()) {
                        std::vector<char> _val;
                        _val.reserve(_storedKey.length());
                        if (_storedKey.length() > _value.mv_size) {
                          LZ4_decompress_safe(static_cast<char*>(_value.mv_data), _val.data(), _value.mv_size, _val.capacity());
                        }
                        else {
                          // Stored value is uncompressed.
                          memcpy(_val.data(), static_cast<char*>(_value.mv_data), _value.mv_size);
                        }

                        std::stringstream _sstr{std::string(_val.data(), _storedKey.length())};
                        auto _e = cluon::extractEnvelope(_sstr);
                        if (_e.first) {
                          // Extract value from Envelope and check whether location is within polygon.
                          const auto _tmp = cluon::extractMessage<opendlv::proxy::GeodeticWgs84Reading>(std::move(_e.second));

                          // Compute Morton code from WGS84 coordinate to allow for geographical queries.
                          {
                            std::stringstream _dataType_senderStamp;
                            _dataType_senderStamp << opendlv::proxy::GeodeticWgs84Reading::ID() << '/' << _e.second.senderStamp() << "-morton";
                            const std::string _shortKey{_dataType_senderStamp.str()};

                            auto morton = convertLatLonToMorton(std::make_pair(_tmp.latitude(), _tmp.longitude()));
                            if (VERBOSE) {
                              std::cerr << _tmp.latitude() << ", " << _tmp.longitude() << " = " << morton << ", " << _storedKey.timeStamp() << std::endl;
                            }

                            // Store data.
                            {
                              // key is the morton code in network byte order
                              MDB_val __key;
                              __key.mv_size = sizeof(morton);
                              morton = htobe64(morton);
                              __key.mv_data = &morton;

                              // value is the nanosecond timestamp in network byte order of the entry from table 'all'
                              MDB_val __value;
                              int64_t _timeStamp = _storedKey.timeStamp();
                              _timeStamp = htobe64(_timeStamp);
                              __value.mv_size = sizeof(_timeStamp);
                              __value.mv_data = &_timeStamp;

                              if (!dbGeodeticWgs84SenderStamp_open) {
                                dbGeodeticWgs84SenderStamp = lmdb::dbi::open(txndbAll, _shortKey.c_str(), MDB_CREATE|MDB_DUPSORT|MDB_DUPFIXED);
                                dbGeodeticWgs84SenderStamp.set_compare(txndbAll, &compareMortonKeys);
                                lmdb::dbi_set_dupsort(txndbAll, dbGeodeticWgs84SenderStamp.handle(), &compareKeys);
                                dbGeodeticWgs84SenderStamp_open = true;
                              }

                              lmdb::dbi_put(txndbAll, dbGeodeticWgs84SenderStamp.handle(), &__key, &__value, 0); 
                            }
                          }
                        }
                      }
                    }
                  }

                  // Store key also in database datatype/senderStamp:
                  {
                    MDB_val __key;
                    __key.mv_size = f.first.size();
                    __key.mv_data = f.first.data();

                    MDB_val __value;
                    __value.mv_size = 0;
                    __value.mv_data = nullptr;

                    const char *__ptr = static_cast<char*>(__key.mv_data);
                    cabinet::Key __storedKey = getKey(__ptr, __key.mv_size);

                     std::stringstream _dataType_senderStamp;
                    _dataType_senderStamp << __storedKey.dataType() << '/' << __storedKey.senderStamp();
                    const std::string _shortKey{_dataType_senderStamp.str()};

                    if (!dbDataTypeSenderStamp_open) {
                      dbDataTypeSenderStamp = lmdb::dbi::open(txndbAll, _shortKey.c_str(), MDB_CREATE);
                      dbDataTypeSenderStamp.set_compare(txndbAll, &compareKeys);
                      dbDataTypeSenderStamp_open = true;
                    }
                    lmdb::dbi_put(txndbAll, dbDataTypeSenderStamp.handle(), &__key, &__value, 0); 
                  }
                }

                // Store temporal information about the trip.
                if (0 != tripStart) {
                  std::cout << tripStart << " --> " << tripEnd << ": " << std::setprecision(10) << posTripStart[0] << "," << posTripStart[1] << " --> " << posTripEnd[0] << "," << posTripEnd[1] << std::setprecision(4) << std::endl;

                  // 2.3 Store start/end time stamps.
                  {
                    const std::string _shortKey{"trips"};

                    auto dbTrips = lmdb::dbi::open(txndbAll, _shortKey.c_str(), MDB_CREATE);
                    dbTrips.set_compare(txndbAll, &compareKeys);
                    {
                      // key is the cabinet::Key from the trip start.
                      MDB_val __key;
                      std::vector<char> _key;
                      const uint64_t MAXKEYSIZE = 511;
                      _key.reserve(MAXKEYSIZE);
                      __key.mv_size = setKey(keyTripStart, _key.data(), _key.capacity());
                      __key.mv_data = _key.data();

                      // value is the cabinet::Key from the last key within the geofence area.
                       MDB_val __value;
                      std::vector<char> _value;
                      _value.reserve(MAXKEYSIZE);
                      __value.mv_size = setKey(keyTripEnd, _value.data(), _value.capacity());
                      __value.mv_data = _value.data();

                      lmdb::dbi_put(txndbAll, dbTrips.handle(), &__key, &__value, 0);
                    }
                  }


                  // 2.4 Spit out GPX trip data.
                  if (GPX) {
                    struct tm ts;
                    char buf[80];
                    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
                    time_t timestamp = e.second.sampleTimeStamp().seconds();
                    ts = *localtime(&timestamp);
                    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ts);
                    std::string t(buf);

                    // Last valid position.
                    sstrGPX << "<trkpt lat=\"" << std::setprecision(8) << currentPos[0] << "\" lon=\"" << currentPos[1] << "\">"
                            << "<ele>" << 0 << "</ele><time>" << t << "</time></trkpt>" << std::endl;
                    sstrGPX << std::string(GPX_FOOTER);

                    std::fstream fout(filenameGPX.c_str(), std::ios::out|std::ios::trunc);
                    fout << sstrGPX.str();
                    fout.flush();
                    fout.close();

                  }
                }
                kept += bufferedKeyValues.size();

                // Drop the temporary tables.
                dbGeodeticWgs84SenderStamp = lmdb::dbi::open(txndbAll, "txnWgs84tmp", MDB_CREATE|MDB_DUPSORT|MDB_DUPFIXED);
                dbGeodeticWgs84SenderStamp.drop(txndbAll, true);
                dbDataTypeSenderStamp = lmdb::dbi::open(txndbAll, "txnDTTStmp", MDB_CREATE);
                dbDataTypeSenderStamp.drop(txndbAll, true);
                txndbAll.commit();
              }
              else {
                // This trip does not meet the MIN_LEN/MAX_LEN criteria.
                skipped += bufferedKeyValues.size();
              }

              // 3. Clear bufferedKeyValues.
              bufferedKeyValues.clear();

              // Reset the filename for the GPX data to allow for next trip data.
              filenameGPX = "";
              sstrGPX.str("");

              // Flag that we did not store the first valid GPS coordinate for the next trip.
              firstValidGPSLocationStored = false;
              tripStart = 0;
            }
          }
        } else {
          invalid++;
        }
      }
      else {
        // This data sample occurred temporally after the last GPS location that was inside the geofence.
        // Hence, we can only dump this data sample when the next GPS location that comes is also inside the geofence.
        // Therefore, we need to buffer for now.
        std::vector<char> keyToBuffer;
        keyToBuffer.reserve(key.mv_size);
        std::copy(static_cast<char*>(key.mv_data), static_cast<char*>(key.mv_data) + key.mv_size, std::back_inserter(keyToBuffer));

        std::vector<char> valueToBuffer;
        valueToBuffer.reserve(value.mv_size);
        std::copy(static_cast<char*>(value.mv_data), static_cast<char*>(value.mv_data) + value.mv_size, std::back_inserter(valueToBuffer));

        auto p = std::make_pair(keyToBuffer, valueToBuffer);
        bufferOfKeyValuePairsToStore.push_back(p);
      }

      const int32_t percentage = static_cast<int32_t>((static_cast<float>(entries) * 100.0f) / static_cast<float>(totalEntries));
      if ((percentage % 5 == 0) && (percentage != oldPercentage)) {
        std::clog <<"Processed " << percentage << "% (" << entries << " entries, kept: " << kept << ", skipped: " << skipped << ", invalid: " << invalid << ") from " << CABINET << std::endl;
        oldPercentage = percentage;
      }
    }
    cursor.close();
    rotxn.abort();
  }
  catch (...) {
    failed = true;
  }
  return failed;
}

#endif
