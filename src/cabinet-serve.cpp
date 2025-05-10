/*
 * Copyright (C) 2025  Christian Berger
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "cluon-complete.hpp"
#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"
#include "key.hpp"
#include "morton.hpp"
#include "lmdb++.h"
#include "lz4.h"
#include "httplib.h"
#include "json.3.12.0.hpp"

#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if (0 == commandlineArguments.count("cab")) {
    std::cerr << argv[0] << " provides a REST interface to a cabinet (an lmdb-based key/value-database)." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cab=myStore.cab [--mem=32024] [--verbose]" << std::endl;
    std::cerr << "         --cab:     name of the database file" << std::endl;
    std::cerr << "         --mem:     upper memory size for database in memory in GB, default: 64,000 (representing 64TB)" << std::endl;
    std::cerr << "         --port:    http port, default: 8080" << std::endl;
    std::cerr << "         --unix:    path to set up a UNIX domain socket (any value to --port will be ignored, existing socket fiels will be erased before and after), default: unset" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cab=myStore.cab" << std::endl;
    std::cerr << "         " << argv[0] << " --cab=myStore.cab --unix=/tmp/cab.sock" << std::endl;
    std::cerr << std::endl;
    std::cerr << " Query all tables via HTTP/TCP socket:    curl http://localhost:<PORT>/tables" << std::endl;
    std::cerr << " Query all tables via UNIX domain socket: curl --no-buffer -XGET --unix-socket <UNIX> http://localhost/tables" << std::endl;
    retCode = 1;
  } else {
    const bool VERBOSE{(commandlineArguments["verbose"].size() != 0)};
    const std::string CABINET{commandlineArguments["cab"]};
    const uint64_t MEM{(commandlineArguments["mem"].size() != 0) ? static_cast<uint64_t>(std::stoi(commandlineArguments["mem"])) : 64UL*1024UL};
    const uint64_t PORT{(commandlineArguments["port"].size() != 0) ? static_cast<uint64_t>(std::stoi(commandlineArguments["port"])) : 8080};
    const std::string UNIX{commandlineArguments["unix"]};

    using json = nlohmann::json;

    httplib::Server svr;

    bool failed{false};
    try {
      auto env = lmdb::env::create();
      env.set_mapsize(MEM * 1024UL * 1024UL * 1024UL);
      env.set_max_dbs(100);
      env.open(CABINET.c_str(), MDB_RDONLY|MDB_NOSUBDIR, 0600);

      // Fetch key/value pairs in a read-only transaction.
      auto rotxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);

      //auto dbiAll = lmdb::dbi::open(rotxn, "all");
      //dbiAll.set_compare(rotxn, &compareKeys);
      //std::clog << "Found " << dbiAll.size(rotxn) << " entries in database 'all'." << std::endl;

      // Response to list all tables.
      svr.Get("/tables",
        [&rotxn, VERBOSE](const httplib::Request &, httplib::Response &res) {
          auto dbilist = lmdb::dbi::open(rotxn);
          auto cursor = lmdb::cursor::open(rotxn, dbilist);
          MDB_val key;
          MDB_val value;
          std::string s;
          json j;
          while (cursor.get(&key, &value, MDB_NEXT)) {
            const char *ptr = static_cast<char*>(key.mv_data);
            s = std::string(ptr);
            j.push_back(s);
            if (VERBOSE) {
              std::clog << s << std::endl;
            }
          }
          s = j.dump();
          res.set_content(s, "application/json");
        }
      );

      // Response to query the number of entries in a table.
      svr.Get("/table/:dbname/entries",
      [&rotxn, VERBOSE](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        uint64_t entries{0};
        try {
          auto dbi = lmdb::dbi::open(rotxn, dbname.c_str());
          entries = dbi.size(rotxn);
          if (VERBOSE) {
            std::clog << "Found " << entries << " entries in database '" << dbname << "'." << std::endl;
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j[dbname]["entries"] = entries;
        std::string s = j.dump();
        res.set_content(s, "application/json");
      });

      // Response to get a particular key in ascending order.
      svr.Get("/table/:dbname/keyentry/:keyentryid",
      [&rotxn, VERBOSE](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const uint64_t KEYENTRYID{static_cast<uint64_t>(std::stoll(req.path_params.at("keyentryid")))};
        std::string keyAsJSON{""};
        try {
          auto dbi = lmdb::dbi::open(rotxn, dbname.c_str());
          dbi.set_compare(rotxn, &compareKeys);
          const uint64_t ALL_ENTRIES = dbi.size(rotxn);
          MDB_val key;
          MDB_val value;
          uint64_t entry{0};
          if (KEYENTRYID < ALL_ENTRIES) {
            // Loop until entry.
            auto cursor = lmdb::cursor::open(rotxn, dbi);
            while ((entry++ < KEYENTRYID) && 
                   cursor.get(&key, &value, MDB_NEXT));

            const char *ptr = static_cast<char*>(key.mv_data);
            cabinet::Key storedKey = getKey(ptr, key.mv_size);
            cluon::ToJSONVisitor jsonVisitor;
            storedKey.accept(jsonVisitor);
            keyAsJSON = jsonVisitor.json();
          }

          if (VERBOSE) {
            std::clog << "Retrieving key " << KEYENTRYID << " from database '" << dbname << "': " << keyAsJSON << std::endl;
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j[dbname]["keyentry"][std::to_string(KEYENTRYID)] = json::parse(keyAsJSON.size() == 0 ? "None" : keyAsJSON);
        std::string s = j.dump();
        res.set_content(s, "application/json");
      });

      // Response to query for a particular key using the key's timestamp.
      svr.Get("/table/:dbname/key/:timestamp",
      [&rotxn, VERBOSE](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const int64_t TIMESTAMP{static_cast<int64_t>(std::stoll(req.path_params.at("timestamp")))};
        std::string keyAsJSON{""};
        try {
          auto dbi = lmdb::dbi::open(rotxn, dbname.c_str());
          dbi.set_compare(rotxn, &compareKeys);

          if (TIMESTAMP > 0) {
            const uint64_t MAXKEYSIZE = 511;
            std::vector<char> _key;
            _key.reserve(MAXKEYSIZE);

            cabinet::Key query;
            query.timeStamp(TIMESTAMP);
            
            MDB_val key;
            key.mv_size = setKey(query, _key.data(), _key.capacity());
            key.mv_data = _key.data();

            MDB_val value;

            auto cursor = lmdb::cursor::open(rotxn, dbi);
            if(cursor.get(&key, &value, MDB_SET_RANGE)) {
              const char *ptr = static_cast<char*>(key.mv_data);
              cabinet::Key storedKey = getKey(ptr, key.mv_size);
              if (TIMESTAMP == storedKey.timeStamp()) {
                cluon::ToJSONVisitor jsonVisitor;
                storedKey.accept(jsonVisitor);
                keyAsJSON = jsonVisitor.json();
                if (VERBOSE) {
                  std::clog << "Retrieving key for timestamp " << TIMESTAMP << " from database '" << dbname << "': " << keyAsJSON << std::endl;
                }
              }
            }
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j[dbname]["key"][std::to_string(TIMESTAMP)] = json::parse(keyAsJSON.size() == 0 ? "None" : keyAsJSON);
        std::string s = j.dump();
        res.set_content(s, "application/json");
      });

      if (UNIX.size() == 0) {
        svr.listen("0.0.0.0", PORT);
      }
      else {
        ::unlink(UNIX.c_str());
        svr.set_address_family(AF_UNIX).listen(UNIX, 80);
        ::unlink(UNIX.c_str());
      }
      //auto dbi = lmdb::dbi::open(rotxn, DB.c_str());
      //dbi.set_compare(rotxn, &compareKeys);
      //const uint64_t totalEntries = dbi.size(rotxn);
      //std::clog << "Found " << totalEntries << " entries in database '" << DB << "'." << std::endl;
/*
      auto cursor = lmdb::cursor::open(rotxn, dbi);

      MDB_val key;
      MDB_val value;
      int32_t oldPercentage{-1};
      uint64_t entries{0};
      while (cursor.get(&key, &value, MDB_NEXT)) {
        entries++;

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
        std::cout.write(static_cast<char*>(val.data()), storedKey.length());

   
        // Extract an Envelope and its payload on the example for AccelerationReading
        std::stringstream sstr{std::string(val.data(), storedKey.length())};
        auto e = cluon::extractEnvelope(sstr);
        if (e.first && e.second.dataType() == opendlv::proxy::AccelerationReading::ID()) {
          const auto tmp = cluon::extractMessage<opendlv::proxy::AccelerationReading>(std::move(e.second));
          std::cerr << tmp.accelerationX() << ", " << tmp.accelerationY() << std::endl;
        }
  

        const int32_t percentage = static_cast<int32_t>((static_cast<float>(entries) * 100.0f) / static_cast<float>(totalEntries));
        if ((percentage % 5 == 0) && (percentage != oldPercentage)) {
          std::clog <<"Processed " << percentage << "% (" << entries << " entries) from " << CABINET << std::endl;
          oldPercentage = percentage;
        }
      }
      cursor.close();
*/
      rotxn.abort();
    }
    catch (...) {
      failed = true;
    }
    return failed;

  }
  return retCode;
}
