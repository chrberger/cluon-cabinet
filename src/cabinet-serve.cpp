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

/**
This application encapsulates the access to a cabinet database in lmdb format and exposes the access via REST interface.
The data can be accessed either via HTTP requests to a TCP socket, or via a UNIX domain sockets.

Starting the server with HTTP/TCP access at port 8085:
    cabinet-serve --cab=database.cab --port=8085 --verbose --odvd=messages.odvd

Starting the server with UNIX domain socket access:
    cabinet-serve --cab=database.cab --port=8085 --verbose --unix=/tmp/cab.sock --odvd=messages.odvd

API calls (examples are shown for curl accessing the UNIX domain socket):

1. Get a list of all tables:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/tables

2. Get the number of rows in table "all":
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/entries

3. Get the first key in table "all" (note that entry 0 is empty):
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/keyentry/1

4. Get the first key in table "all" (note that entry 0 is empty) as raw, base64-encoded export:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/keyentry/1/raw

5. Get the key for timestamp t in table "all"; the timestamp is given in UNIX Epoch in nanoseconds:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/key/1680168559268570000

6. Get the key for timestamp t in table "all" as raw, base64-encoded export; the timestamp is given in UNIX Epoch in nanoseconds:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/key/1680168559268570000/raw

7. Get the value for the key with timestamp t in table "all"; the timestamp is given in UNIX Epoch in nanoseconds by using the .odvd message specification to resolve the content:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/value/1680168559268570000

8. Get the value for the key with timestamp t in table "all" as raw, base64-encoded export; the timestamp is given in UNIX Epoch in nanoseconds by using the .odvd message specification to resolve the content:
  curl --no-buffer -XGET --unix-socket /tmp/cab.sock http://localhost/v1/table/all/value/1680168559268570000/raw
 */
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

    cluon::MessageParser mp;
    std::pair<std::vector<cluon::MetaMessage>, cluon::MessageParser::MessageParserErrorCodes> messageParserResult;
    {
      std::ifstream fin(commandlineArguments["odvd"], std::ios::in|std::ios::binary);
      if (fin.good()) {
        std::string input(static_cast<std::stringstream const&>(std::stringstream() << fin.rdbuf()).str()); // NOLINT
        fin.close();
        messageParserResult = mp.parse(input);
        std::clog << "Found " << messageParserResult.first.size() << " messages." << std::endl;
      }
      else {
        std::cerr << argv[0] << ": Message specification '" << commandlineArguments["odvd"] << "' not found." << std::endl;
        return retCode = 1;
      }
    }
    std::map<int32_t, cluon::MetaMessage> scope;
    for (const auto &e : messageParserResult.first) { scope[e.messageIdentifier()] = e; }

    using json = nlohmann::json;

    httplib::Server svr;

    bool failed{false};
    try {
      auto env = lmdb::env::create();
      env.set_mapsize(MEM * 1024UL * 1024UL * 1024UL);
      env.set_max_dbs(100);
      env.open(CABINET.c_str(), MDB_RDONLY|MDB_NOSUBDIR, 0600);

      // Interact with the lmdb tables in a read-only transaction.
      auto rotxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);

      // Response to list all tables.
      svr.Get("/v1/tables",
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
          cursor.close();
        }
      );

      // Response to query the number of entries in a table.
      svr.Get("/v1/table/:dbname/entries",
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
        j["rows"] = entries;
        std::string s = j.dump();
        res.set_content(s, "application/json");
      });

      // Lambda to call for exporting a key, either as JSONified object or as base64-encoded raw bytes.
      auto retrieveKeyEntryByID = 
      [&rotxn, VERBOSE](const std::string &dbname, const uint64_t KEYENTRYID, const bool &AS_RAW) {
        std::string keyAsJSON{""};
        try {
          auto dbi = lmdb::dbi::open(rotxn, dbname.c_str());
          dbi.set_compare(rotxn, &compareKeys);
          const uint64_t ALL_ENTRIES = dbi.size(rotxn);
          if (KEYENTRYID < ALL_ENTRIES) {
            MDB_val key;
            MDB_val value;
            // Loop until entry.
            uint64_t entry{0};
            auto cursor = lmdb::cursor::open(rotxn, dbi);
            while ((entry++ < KEYENTRYID) && 
                   cursor.get(&key, &value, MDB_NEXT));

            const char *ptr = static_cast<char*>(key.mv_data);
            if (AS_RAW) {
              const std::string DATA(reinterpret_cast<const char *>(ptr), key.mv_size);
              keyAsJSON = "{\"raw_as_base64\":\"" + cluon::ToJSONVisitor::encodeBase64(DATA) + "\"}";
            }
            else {
              cabinet::Key storedKey = getKey(ptr, key.mv_size);
              cluon::ToJSONVisitor jsonVisitor;
              storedKey.accept(jsonVisitor);
              keyAsJSON = jsonVisitor.json();
            }
            cursor.close();
          }

          if (VERBOSE) {
            std::clog << "Retrieving key " << KEYENTRYID << " from database '" << dbname << "': " << keyAsJSON << std::endl;
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j = json::parse(keyAsJSON.size() == 0 ? "{}" : keyAsJSON);
        std::string s = j.dump();
        return s;
      };

      // Response to get a particular key in ascending order and return the key in base64-encoded raw bytes.
      svr.Get("/v1/table/:dbname/keyentry/:keyentryid/raw",
      [&retrieveKeyEntryByID](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const uint64_t KEYENTRYID{static_cast<uint64_t>(std::stoll(req.path_params.at("keyentryid")))};
        const bool AS_RAW{true};
        std::string s = retrieveKeyEntryByID(dbname, KEYENTRYID, AS_RAW);
        res.set_content(s, "application/json");
       });

      // Response to get a particular key in ascending order.
      svr.Get("/v1/table/:dbname/keyentry/:keyentryid",
      [&retrieveKeyEntryByID](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const uint64_t KEYENTRYID{static_cast<uint64_t>(std::stoll(req.path_params.at("keyentryid")))};
        const bool AS_RAW{false};
        std::string s = retrieveKeyEntryByID(dbname, KEYENTRYID, AS_RAW);
        res.set_content(s, "application/json");
       });

      // Lambda to call for exporting a key given as timestamp, either as JSONified object or as base64-encoded raw bytes.
      auto retrieveKeyEntryByTimeStamp = 
      [&rotxn, VERBOSE](const std::string &dbname, const int64_t TIMESTAMP, const bool &AS_RAW) {
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
                if (AS_RAW) {
                  const std::string DATA(reinterpret_cast<const char *>(ptr), key.mv_size);
                  keyAsJSON = "{\"raw_as_base64\":\"" + cluon::ToJSONVisitor::encodeBase64(DATA) + "\"}";
                }
                else {
                  cluon::ToJSONVisitor jsonVisitor;
                  storedKey.accept(jsonVisitor);
                  keyAsJSON = jsonVisitor.json();
                }
 
                if (VERBOSE) {
                  std::clog << "Retrieving key for timestamp " << TIMESTAMP << " from database '" << dbname << "': " << keyAsJSON << std::endl;
                }
              }
            }
            cursor.close();
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j = json::parse(keyAsJSON.size() == 0 ? "{}" : keyAsJSON);
        std::string s = j.dump();
        return s;
      };

      // Response to query for a particular key using the key's timestamp and to return the result as raw, base64-encoded bytes.
      svr.Get("/v1/table/:dbname/key/:timestamp/raw",
      [&retrieveKeyEntryByTimeStamp](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const int64_t TIMESTAMP{static_cast<int64_t>(std::stoll(req.path_params.at("timestamp")))};
        const bool AS_RAW{true};
        std::string s = retrieveKeyEntryByTimeStamp(dbname, TIMESTAMP, AS_RAW);
        res.set_content(s, "application/json");
      });

      // Response to query for a particular key using the key's timestamp.
      svr.Get("/v1/table/:dbname/key/:timestamp",
      [&retrieveKeyEntryByTimeStamp](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const int64_t TIMESTAMP{static_cast<int64_t>(std::stoll(req.path_params.at("timestamp")))};
        const bool AS_RAW{false};
        std::string s = retrieveKeyEntryByTimeStamp(dbname, TIMESTAMP, AS_RAW);
        res.set_content(s, "application/json");
      });

      // Lambda to call for exporting a key given as timestamp, either as JSONified object or as base64-encoded raw bytes.
      auto retrieveValueByTimeStamp = 
      [&rotxn, VERBOSE, &messageParserResult, &scope](const std::string &dbname, const int64_t TIMESTAMP, const bool &AS_RAW) {
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
                std::vector<char> val;
                val.reserve(storedKey.length());
                if (storedKey.length() > value.mv_size) {
                  LZ4_decompress_safe(static_cast<char*>(value.mv_data), val.data(), value.mv_size, val.capacity());
                }
                else {
                  // Stored value is uncompressed.
                  memcpy(val.data(), static_cast<char*>(value.mv_data), value.mv_size);
                }

                if (AS_RAW) {
                  const std::string DATA(reinterpret_cast<const char *>(val.data()), storedKey.length());
                  keyAsJSON = "{\"raw_as_base64\":\"" + cluon::ToJSONVisitor::encodeBase64(DATA) + "\"}";
                }
                else {
                  std::stringstream sstr{std::string(val.data(), storedKey.length())};
                  auto e = cluon::extractEnvelope(sstr);
                  if (e.first) {
                    cluon::data::Envelope env{std::move(e.second)};
                    if (scope.count(env.dataType()) > 0) {
                      cluon::FromProtoVisitor protoDecoder;
                      std::stringstream _sstr(env.serializedData());
                      protoDecoder.decodeFrom(_sstr);

                      cluon::MetaMessage m = scope[env.dataType()];
                      cluon::GenericMessage gm;
                      gm.createFrom(m, messageParserResult.first);
                      gm.accept(protoDecoder);
              
                      cluon::ToJSONVisitor jsonVisitor;
                      gm.accept(jsonVisitor);
                      keyAsJSON = jsonVisitor.json();

                      json j;
                      j = {
                        { "dataType", env.dataType() },
                        { "senderStamp", env.senderStamp() },
                        { "sent", cluon::time::toMicroseconds(env.sent()) },
                        { "received", cluon::time::toMicroseconds(env.sent()) },
                        { "sampleTimeStamp", cluon::time::toMicroseconds(env.sampleTimeStamp()) },
                        { "typeName", m.packageName() + m.messageName() },
                        { "message", json::parse(keyAsJSON) }
                      };
                      keyAsJSON = j.dump();
                    }
                  }
                }
 
                if (VERBOSE) {
                  std::clog << "Retrieving value for timestamp " << TIMESTAMP << " from database '" << dbname << "': " << keyAsJSON << std::endl;
                }
              }
            }
            cursor.close();
          }
        }
        catch(...) {
          std::cerr << "Failed to open database '" << dbname << "'." << std::endl;
        }
        json j;
        j = json::parse(keyAsJSON.size() == 0 ? "{}" : keyAsJSON);
        std::string s = j.dump();
        return s;
      };

      // Response to query for a particular key using the key's timestamp and to return the result as raw, base64-encoded bytes.
      svr.Get("/v1/table/:dbname/value/:timestamp/raw",
      [&retrieveValueByTimeStamp](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        const int64_t TIMESTAMP{static_cast<int64_t>(std::stoll(req.path_params.at("timestamp")))};
        const bool AS_RAW{true};
        std::string s = retrieveValueByTimeStamp(dbname, TIMESTAMP, AS_RAW);
        res.set_content(s, "application/json");
      });

      // Response to query for a particular key using the key's timestamp.
      svr.Get("/v1/table/:dbname/value/:timestamp",
      [&retrieveValueByTimeStamp](const httplib::Request &req, httplib::Response &res) {
        auto dbname = req.path_params.at("dbname");
        std::replace(dbname.begin(), dbname.end(), '_', '/');
        if (dbname != "all") {
          std::cerr << "Warning! Retrieving values for tables other than all may not work." << std::endl;
        }
        const int64_t TIMESTAMP{static_cast<int64_t>(std::stoll(req.path_params.at("timestamp")))};
        const bool AS_RAW{false};
        std::string s = retrieveValueByTimeStamp(dbname, TIMESTAMP, AS_RAW);
        res.set_content(s, "application/json");
      });


      // Start listening.
      if (UNIX.size() == 0) {
        svr.listen("0.0.0.0", PORT);
      }
      else {
        ::unlink(UNIX.c_str());
        svr.set_address_family(AF_UNIX).listen(UNIX, 80);
        ::unlink(UNIX.c_str());
      }

      rotxn.abort();
    }
    catch (...) {
      failed = true;
    }
    return failed;

  }
  return retCode;
}
